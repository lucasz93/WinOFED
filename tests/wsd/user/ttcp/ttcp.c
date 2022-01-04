/*
 *	T T C P . C
 *
 * Test TCP connection.  Makes a connection on port 5001
 * and transfers fabricated buffers or data copied from stdin.
 *
 * Ported to MS Windows enviroment, Nov. 1999
 *			Lily Yang, Intel Corp. 
 *	 Always in sinkmode, use only real time for measurement.
 *	So, -s and -v options are not accepted.
 *
 * Usable on 4.2, 4.3, and 4.1a systems by defining one of
 * BSD42 BSD43 (BSD41a)
 * Machines using System V with BSD sockets should define SYSV.
 *
 * Modified for operation under 4.2BSD, 18 Dec 84
 *      T.C. Slattery, USNA
 * Minor improvements, Mike Muuss and Terry Slattery, 16-Oct-85.
 * Modified in 1989 at Silicon Graphics, Inc.
 *	catch SIGPIPE to be able to print stats when receiver has died 
 *	for tcp, don't look for sentinel during reads to allow small transfers
 *	increased default buffer size to 8K, nbuf to 2K to transfer 16MB
 *	moved default port to 5001, beyond IPPORT_USERRESERVED
 *	make sinkmode default because it is more popular, 
 *		-s now means don't sink/source 
 *	count number of read/write system calls to see effects of 
 *		blocking from full socket buffers
 *	for tcp, -D option turns off buffered writes (sets TCP_NODELAY sockopt)
 *	buffer alignment options, -A and -O
 *	print stats in a format that's a bit easier to use with grep & awk
 *	for SYSV, mimic BSD routines to use most of the existing timing code
 * Modified by Steve Miller of the University of Maryland, College Park
 *	-b sets the socket buffer size (SO_SNDBUF/SO_RCVBUF)
 * Modified Sept. 1989 at Silicon Graphics, Inc.
 *	restored -s sense at request of tcs@brl
 * Modified Oct. 1991 at Silicon Graphics, Inc.
 *	use getopt(3) for option processing, add -f and -T options.
 *	SGI IRIX 3.3 and 4.0 releases don't need #define SYSV.
 *
 * Distribution Status -
 *      Public Domain.  Distribution Unlimited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <winsock2.h> 
#include <getopt.h> 

#define DBG_SPEW	0

#if DBG_SPEW
#define DEBUG_OUT(x) { \
        char str[256];\
        sprintf(str, "%s:%d:0x%x:0x%x: %s", "ttcp", __LINE__, GetCurrentProcessId(),\
                                  GetCurrentThreadId(), (x));\
        OutputDebugString(str);\
}

#define DEBUG_OUT1(x,p) { \
        char str[256];\
        sprintf(str, "%s:%d:0x%x:0x%x: buf=0x%p %s", "ttcp", __LINE__, GetCurrentProcessId(),\
                                  GetCurrentThreadId(), (p),(x));\
        OutputDebugString(str);\
}
#else
#define DEBUG_OUT(x)
#define DEBUG_OUT1(x,p)
#endif

struct sockaddr_in sinhim;
struct sockaddr_in frominet;

int domain, fromlen;

int buflen = 8 * 1024;	/* length of buffer */
char *buf;				/* ptr to dynamic buffer */
int nbuf = 2048;		/* number of buffers to send in sinkmode */

int bufoffset = 0;		/* align buffer to this */
int bufalign = 16*1024;		/* modulo this */

int udp = 0;			/* 0 = tcp, !0 = udp */
int options = 0;		/* socket options */
int one = 1;                    /* for 4.3 BSD style setsockopt() */
short port = 5001;		/* TCP port number */
char *host = 0;			/* ptr to name of host */
int trans;				/* 0=receive, !0=transmit mode */
int nodelay = 0;		/* set TCP_NODELAY socket option */
int b_flag = 0;			/* use mread() */
int sockbufsize = 0;		/* socket buffer size to use */
char fmt = 'M';			/* output format: k = kilobits, K = kilobytes,
						 *  m = megabits, M = megabytes, 
						 *  g = gigabits, G = gigabytes */
int touchdata = 0;		/* access data after reading */

#ifndef errno
extern int errno;
#endif

char Usage[] = "\
Usage: ttcp -t [-options] host \n\
       ttcp -r [-options]\n\
Common options:\n\
	-l ##	length of bufs read from or written to network (default 8192)\n\
	-u	use UDP instead of TCP\n\
	-p ##	port number to send to or listen at (default 5001)\n\
	-A	align the start of buffers to this modulus (default 16384)\n\
	-O	start buffers at this offset from the modulus (default 0)\n\
	-d	set SO_DEBUG socket option\n\
	-b ##	set socket buffer size (if supported)\n\
	-f X	format for rate: k,K = kilo{bit,byte}; m,M = mega; g,G = giga\n\
Options specific to -t:\n\
	-n##	number of source bufs written to network (default 2048)\n\
	-D	don't buffer TCP writes (sets TCP_NODELAY socket option)\n\
Options specific to -r:\n\
	-B	for -s, only output full blocks as specified by -l (for TAR)\n\
	-T	\"touch\": access each byte as it's read\n\
";	

double nbytes = 0;			/* bytes on net */
unsigned long numCalls;		/* # of I/O system calls */
double realt;		/* user, real time (seconds) */
SOCKET fd = (SOCKET)SOCKET_ERROR;       
SOCKET fd_orig = (SOCKET)SOCKET_ERROR;

BOOL DoGracefulShutdown(SOCKET sock);
void err();
void mes();
void winsockInit();
void commandLine();
void bind_socket();
void showTime(PSYSTEMTIME pst);
void pattern();
int Nread();
int Nwrite();
int mread();
void delay();
void timer();
double time_elapsed();
char *outfmt();

/*
 * input a scaled (K, M or G) integer. Suitable for use with getopt.
 *
 * inputs:
 *	*s	C string representing an interger value with optional Scale char.
 *		'xK' imples x * 1024, 'xM' implies x*1024*1024,'xG' implies x*1024*1024*1024
 *
 * output:
 *	binary integer representation.
 */

static int
atoi_scaled(char *s)
{
	int	val;
	char *e;

	val = strtol(s,&e,0);
	if (e == NULL || *e =='\0')
		return val;

	if (*e == 'k' || *e == 'K')
		val *= 1024;
	else if (*e == 'm' || *e == 'M')
		val *= 1024*1024;
	else if (*e == 'g' || *e == 'G')
		val *= 1024*1024*1024;

	return val;
}


int __cdecl main(int argc, char **argv)
{
	SYSTEMTIME time0, time1;
	int cnt;

	commandLine(argc, argv);
	winsockInit();

	memset(&sinhim,0,sizeof(sinhim));

	if(trans)  {
		struct hostent * pHostAddr;
		// xmitr 
		memset(&sinhim,0,sizeof(sinhim));
		sinhim.sin_family = AF_INET;
		if (atoi(host) > 0 )  {
			// Numeric 
			sinhim.sin_addr.s_addr = inet_addr(host);
		} else {
			if ((pHostAddr=gethostbyname(host)) == NULL)
				err("bad hostname");
			sinhim.sin_addr.s_addr = *(u_long*)pHostAddr->h_addr;
		}
		sinhim.sin_port = htons(port);
	}


	if (udp && buflen < 5) {
		buflen = 5;		// send more than the sentinel size 
	}

	if ( (buf = (char *)malloc(buflen+bufalign)) == (char *)NULL)
		err("malloc");
	if (bufalign != 0)
		buf +=(bufalign - ((uintptr_t)buf % bufalign) + bufoffset) % bufalign;

	fprintf(stdout, "ttcp PID=0x%x TID=0x%x\n", GetCurrentProcessId(), GetCurrentThreadId());

	if (trans) {
		fprintf(stdout, "ttcp -t: buflen=%d, nbuf=%d, align=%d/%d, port=%d",
				buflen, nbuf, bufalign, bufoffset, port);
		if (sockbufsize)
			fprintf(stdout, ", sockbufsize=%d", sockbufsize);
		fprintf(stdout, "  %s  -> %s\n", udp?"udp":"tcp", host);
	} else {
		fprintf(stdout,
				"ttcp -r: buflen=%d, nbuf=%d, align=%d/%d, port=%d",
				buflen, nbuf, bufalign, bufoffset, port);
		if (sockbufsize)
			fprintf(stdout, ", sockbufsize=%d", sockbufsize);
		fprintf(stdout, "  %s\n", udp?"udp":"tcp");
	}
	DEBUG_OUT(("socket() start.\n"));
	if ((fd = socket(AF_INET, udp?SOCK_DGRAM:SOCK_STREAM, 0)) == INVALID_SOCKET){
		err("socket");
	}
	DEBUG_OUT(("socket() finish.\n"));
	fprintf(stdout, "fd=0x%x\n",fd);

	DEBUG_OUT(("bind_socket() start.\n"));
	if (trans) {
		bind_socket(fd, 0);
	} else {
		bind_socket(fd, port);
	}
	DEBUG_OUT(("bind_socket() finish.\n"));

	/*
	 * CLOSE_ISSUE
	 * If the SO_LINGER option is enabled, then closesocket() seems to wake
	 * up a separate thread (previously spawned by the switch during connect() call) 
	 * after l_linger seconds, and this thread takes care of calling GetOverlappedResult, 
	 * DeregisterMemory, etc. In this case, there is no delay in the WSACleanup()
	 * call, and both the provide WSPCloseSocket() and WSPCleanup() calls are
	 * seen.
	 */
#if 0
	{
		struct linger ling;
		ling.l_onoff = 1;
		ling.l_linger = 3;
		DEBUG_OUT(("setsockopt(SO_LINGER) start.\n"));
		if (setsockopt(fd, SOL_SOCKET, SO_LINGER,
					(const char*)&ling, sizeof(ling)) < 0){
			err("setsockopt: SO_LINGER");
		}
		DEBUG_OUT(("setsockopt(SO_LINGER) finished.\n"));
	}

#endif

	if (sockbufsize) {
		if (trans) {
			if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, 
						(const char*)&sockbufsize,sizeof sockbufsize) < 0)
				err("setsockopt: sndbuf");
		} else {
			if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, 
						(const char*)&sockbufsize,sizeof sockbufsize) < 0)
				err("setsockopt: rcvbuf");
		}
	}

	if (!udp)  {
		if (trans) {
			// We are the client if transmitting 
			if (options)  {
				if( setsockopt(fd, SOL_SOCKET, options, (const char*)&one, 
							sizeof(one)) < 0)
					err("setsockopt");
			}
			if (nodelay) {
				struct protoent *p;
				p = getprotobyname("tcp");
				if( p && setsockopt(fd, p->p_proto, TCP_NODELAY, 
							(const char*)&one, sizeof(one)) < 0)
					err("setsockopt: nodelay");
			}
			DEBUG_OUT(("connect() start.\n"));
			if(connect(fd, (const struct sockaddr *)&sinhim, sizeof(sinhim) ) < 0){
				err("connect");
			}
			DEBUG_OUT(("connect() finished.\n"));
			fprintf(stdout,"ttcp -t: TCP connection established! \n");
		} else {

			// otherwise, we are the server and 
			//should listen for the connections

			DEBUG_OUT(("listen() start.\n"));
			listen(fd,1);   // workaround for alleged u4.2 bug 
			DEBUG_OUT(("listen() finish.\n"));
			if(options)  {
				if( setsockopt(fd, SOL_SOCKET, options, (const char*)&one, 
							sizeof(one)) < 0)
					err("setsockopt");
			}
			fromlen = sizeof(frominet);
			domain = AF_INET;
			fd_orig=fd;

			DEBUG_OUT(("accept() start.\n"));
			if((fd=accept(fd, (struct sockaddr *)&frominet, &fromlen) ) == INVALID_SOCKET){
				err("accept");
			}
			DEBUG_OUT(("accept() finish.\n"));

			{ 
				struct sockaddr_in peer;
				int peerlen = sizeof(peer);

				fprintf(stdout,"closesocket(fd_orig)..\n");
				if (closesocket(fd_orig)==SOCKET_ERROR){
					err("socket close fd_orig!");
					fd_orig = (SOCKET)SOCKET_ERROR;
				}


				if (getpeername(fd, (struct sockaddr *) &peer, 
							&peerlen) < 0) {
					err("getpeername");
				}
				fprintf(stdout,"ttcp -r: accept from %s fd=0x%p\n", 
						inet_ntoa(peer.sin_addr), fd);
			}
		}
	}

	if (trans)  {
		fprintf(stdout,"ttcp -t: start transmitting ...\n");
		pattern( buf, buflen );
		if(udp)  (void)Nwrite( fd, buf, 4 ); // rcvr start 
		timer(&time0);
		while (nbuf-- && Nwrite(fd,buf,buflen) == buflen){
			nbytes += buflen;
#if DBG_SPEW
			fprintf(stdout,"nbytes=%.0f nbuf=%d, buflen=%d\n", nbytes, nbuf, buflen);
#endif
		}
		timer(&time1);
#if DBG_SPEW
		fprintf(stdout,"nbytes=%.0f nbuf=%d\n", nbytes, nbuf);
#endif
		if(udp)  (void)Nwrite( fd, buf, 4 ); // rcvr end 
	} else {

		fprintf(stdout,"ttcp -r: start receiving ...\n");
		if (udp) {
			while ((cnt=Nread(fd,buf,buflen)) > 0)  {
				static int going = 0;
				if( cnt <= 4 )  {
					if( going )
						break;	// "EOF" 
					going = 1;
					timer(&time0);
				} else {
					nbytes += cnt;
				}
			}
		} else {
			timer(&time0);
			while ((cnt=Nread(fd,buf,buflen)) > 0)  {
				nbytes += cnt;
#if DBG_SPEW
				fprintf(stdout,"nbytes=%.0f cnt=%d\n", nbytes, cnt);
#endif
			}
			timer(&time1);
#if DBG_SPEW
			fprintf(stdout,"nbytes=%.0f cnt=%d\n", nbytes, cnt);
#endif
		}
	}

	if(0){
		fprintf(stdout, "Pausing before close...\n");
		_getch();
	}

#if 1
	/* 
         * CLOSE_ISSUE
	 * Calling closesocket() without any linger option results in 
	 * approximately a 15 second delay in the WSACleanup() call, 
	 * with no provider WSPCloseSocket() or WSPCleanup() calls ever 
	 * seen.
	 */
	DEBUG_OUT(("closesocket() start.\n"));
	fprintf(stdout,"closesocket(fd)..\n");
	if (closesocket(fd)==SOCKET_ERROR) {
		err("socket close fd!");
	}
	DEBUG_OUT(("closesocket() finish.\n"));
#else
	/* 
	 * CLOSE_ISSUE
	 * If DoGracefulShutdown is called, then the closesocket() (in DoGracefulShutdown()) 
         * seems to wake up a separate thread (previously spawned by the switch during 
         * connect() call) and this thread takes care of calling GetOverlappedResult,
	 * DeregisterMemory, etc. In this case, there is no delay in the WSACleanup()
	 * call, and both the provide WSPCloseSocket() and WSPCleanup() calls are
	 * seen.
	 */
	DoGracefulShutdown(fd);
#endif


	if(0){
		fprintf(stdout, "Pausing before cleanup...\n");
		_getch();
	}


	DEBUG_OUT(("WSACleanup() start.\n"));	
	fprintf(stdout,"WSACleanup()..\n");
	/* 
	 * CLOSE_ISSUE without SO_LINGER, or DoGracefulShutdown(), WSACleanup()
	 * can hang for 15 seconds. No WSPCloseSocket() or WSPCleanup() calls
	 * are seen in the provider for this case.
	 */
	if (WSACleanup()==SOCKET_ERROR) {
		err("WSACleanup");
	}
	DEBUG_OUT(("WSACleanup() finish.\n"));

	if(0){
		fprintf(stdout, "Pausing after cleanup...\n");
		_getch();
	}

	realt = time_elapsed(&time0, &time1);

	if(udp&&trans)  {
		(void)Nwrite( fd, buf, 4 ); // rcvr end 
		(void)Nwrite( fd, buf, 4 ); // rcvr end 
		(void)Nwrite( fd, buf, 4 ); // rcvr end 
		(void)Nwrite( fd, buf, 4 ); // rcvr end 
	}
	if( realt <= 0.0 )  realt = 0.001;
	fprintf(stdout,
			"ttcp %s: %.0f bytes in %.2f real MilliSeconds = %s/sec +++\n",
			trans?"-t":"-r",
			nbytes, realt, outfmt(nbytes*1000/realt));
	fprintf(stdout,
			"ttcp %s: %d I/O calls, msec/call = %.2f, calls/sec = %.2f\n",
			trans?"-t":"-r",
			numCalls,
			realt/((double)numCalls),
			((double)(1000*numCalls))/realt);
	fflush(stdout);

#if 0
	if(1){
		fprintf(stdout, "Pausing before return...\n");
		_getch();
	}
#endif  

	return(0);
}

void err(char *s)
{
	fprintf(stderr,"ERROR -- ttcp %s: ", trans?"-t":"-r");
	fprintf(stderr,"%s\n",s);
	fprintf(stderr, "Cleaning up\n");
	if(fd!=SOCKET_ERROR){
		if (closesocket(fd)==SOCKET_ERROR)
			fprintf(stderr, "socket close fd!");
	}

	fprintf(stderr,"\n");
	exit(-1);
}

void mes(char *s)
{
	fprintf(stdout,"ttcp %s: %s\n", trans?"-t":"-r", s);
	return;
}

void pattern(char *cp, int cnt)
{
	register char c;
	c = 0;
	while( cnt-- > 0 )  {
		while( !isprint((c&0x7F)) )  c++;
		*cp++ = (c++&0x7F);
	}
}

char *outfmt(double b)
{
    static char obuf[50];
    switch (fmt) {
	case 'G':
	    sprintf_s(obuf, sizeof(obuf), "%.2f GByte", b / 1024.0 / 1024.0 / 1024.0);
	    break;
	default:
	case 'K':
	    sprintf_s(obuf, sizeof(obuf),  "%.2f KByte", b / 1024.0);
	    break;
	case 'M':
	    sprintf_s(obuf, sizeof(obuf),  "%.2f MByte", b / 1024.0 / 1024.0);
	    break;
	case 'g':
	    sprintf_s(obuf, sizeof(obuf),  "%.2f Gbit", b * 8.0 / 1024.0 / 1024.0 / 1024.0);
	    break;
	case 'k':
	    sprintf_s(obuf, sizeof(obuf),  "%.2f Kbit", b * 8.0 / 1024.0);
	    break;
	case 'm':
	    sprintf_s(obuf, sizeof(obuf),  "%.2f Mbit", b * 8.0 / 1024.0 / 1024.0);
	    break;
    }
    return obuf;
}

/*
 *			T I M E R
 */
void timer(PSYSTEMTIME time)
{
	GetSystemTime(time);
//	showTime(time);
}
/*
 *			N R E A D
 */
int Nread(int fd, void *buf, int count)
{
	struct sockaddr_in from;
	int len = sizeof(from);
	register int cnt;
	if( udp )  {
		cnt = recvfrom( fd, buf, count, 0, (struct sockaddr *)&from, &len );
		numCalls++;
	} else {
		if( b_flag )
			cnt = mread( fd, buf, count );	// fill buf 
		else {
			DEBUG_OUT(("recv() start.\n"));
			cnt = recv(fd, buf, count, 0);
			DEBUG_OUT(("recv() finish.\n"));
			numCalls++;
		}
		if (touchdata && cnt > 0) {
			register int c = cnt, sum=0;
			register char *b = buf;
			while (c--)
				sum += *b++;
		}
	}
	return(cnt);
}

/*
 *			N W R I T E
 */
int Nwrite(int fd, void *buf, int count)
{
	register int cnt;
	if( udp )  {
again:
		cnt = sendto( fd, buf, count, 0, (const struct sockaddr *)&sinhim,
			sizeof(sinhim) );
		numCalls++;
		if( cnt<0 && WSAGetLastError() == WSAENOBUFS )  {
			delay(18000);
			goto again;
		}
	} else {
		DEBUG_OUT1(("send() start.\n"),buf);
		cnt = send(fd, buf, count, 0);
		DEBUG_OUT1(("send() finish.\n"),buf);
		numCalls++;
	}
	return(cnt);
}

void delay(int us)
{
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = us;
	(void)select( 1, NULL, NULL, NULL, &tv );
}

/*
 *			M R E A D
 *
 * This function performs the function of a read(II) but will
 * call read(II) multiple times in order to get the requested
 * number of characters.  This can be necessary because
 * network connections don't deliver data with the same
 * grouping as it is written with.  Written by Robert S. Miles, BRL.
 */
int mread(int fd, char *bufp, unsigned int n)
{
	register unsigned	count = 0;
	register int		nread;

	do {
		nread = recv(fd, bufp, n-count, 0);
		numCalls++;
		if(nread < 0)  {
			perror("ttcp_mread");
			return(-1);
		}
		if(nread == 0)
			return((int)count);
		count += (unsigned)nread;
		bufp += nread;
	 } while(count < n);

	return((int)count);
}

void winsockInit() 
{
	// initialization of the winsock DLL first
	WORD wVersionRequested;
	WSADATA wsaData;
	int ret;
 
	wVersionRequested = MAKEWORD( 2, 0 );
	ret = WSAStartup( wVersionRequested, &wsaData );
	if ( ret != 0 ) {
		// Tell the user that we couldn't find a usable 
		// WinSock DLL.                                  
		err(" Winsock Error!");
	}
 
	// Confirm that the WinSock DLL supports 2.0.
	// Note that if the DLL supports versions greater    
	// than 2.0 in addition to 2.0, it will still return 
	// 2.0 in wVersion since that is the version we      
	// requested.                                        
 
	if ( LOBYTE( wsaData.wVersion ) != 2 ||
			HIBYTE( wsaData.wVersion ) != 0 ) {
		// Tell the user that we couldn't find a usable 
		// WinSock DLL.                                  
		WSACleanup( );
		err(" Winsock Error!");
	}
	return;
 
}

void commandLine(int argc, char ** argv)
{
	int i;

	if (argc < 2) goto usage;

	while ((i=getopt(argc, argv, "BtrdDn:l:p:uA:O:b:f:T?")) != -1)
	{
		switch (i)
		{
		case 'B':
			b_flag = 1;
			break;
		case 't':
			trans = 1;
			break;
		case 'r':
			trans = 0;
			break;
		case 'd':
			options |= SO_DEBUG;
			break;
		case 'D':
	#ifdef TCP_NODELAY
			nodelay = 1;
	#else
			fprintf(stderr, 
	"ttcp: -D option ignored: TCP_NODELAY socket option not supported\n");
	#endif
			break;
		case 'n':
			nbuf = atoi_scaled(optarg);
			break;
		case 'l':
			buflen = atoi_scaled(optarg);
			break;
		case 'p':
			port = (short)atoi(optarg);
			break;
		case 'u':
			udp = 1;
			break;
		case 'A':
			bufalign = atoi_scaled(optarg);
			break;
		case 'O':
			bufoffset = atoi_scaled(optarg);
			break;
		case 'b':
	#if defined(SO_SNDBUF) || defined(SO_RCVBUF)
			sockbufsize = atoi_scaled(optarg);
	#else
			fprintf(stderr, 
	"ttcp: -b option ignored: SO_SNDBUF/SO_RCVBUF socket options not supported\n");
	#endif
			break;
		case 'f':
			fmt = *optarg;
			break;
		case 'T':
			touchdata = 1;
			break;
	
		case '?':
		default:
			goto usage;
		}
	}

	if(trans)  
		host = argv[argc-1];
	return;
usage:
	fprintf(stdout,Usage);
	exit(-1);
}


// binding for receiver
void bind_socket(int fd, const short portNum)
{
	const int hostnameLen = 100;
	char *hostname = (char *)malloc(hostnameLen);
	struct sockaddr_in  addr;
	struct hostent * pHostAddr;
	if (hostname==NULL) err("malloc error!");

	// address binding
	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(portNum);

	if (gethostname(hostname, hostnameLen) == SOCKET_ERROR ) {
		err("socket error!");
	}

#if 0
	pHostAddr = gethostbyname(hostname);
	if (!pHostAddr) {
		err("socket error!");
	} else { // print out the dotted IP address
		addr.sin_addr.s_addr = *(u_long*)pHostAddr->h_addr;
	}
#else
	addr.sin_addr.s_addr = INADDR_ANY;
#endif

		// bind with the socket
	if (bind(fd, (const struct sockaddr*)&addr, 
		sizeof(addr)) == SOCKET_ERROR) {
		err("socket error!");
	}
	free(hostname);
	return;

}

void showTime(PSYSTEMTIME pst)
{
	fprintf(stdout, "It is now %02u:%02u:%02u:%03u on  %02u/%02u/%4u.\n",
		pst->wHour, pst->wMinute, pst->wSecond, pst->wMilliseconds, \
		pst->wMonth, pst->wDay, pst->wYear);
}

double time_elapsed(PSYSTEMTIME pst0,PSYSTEMTIME pst1)
{
	double diff=0;
	diff += pst1->wMilliseconds - pst0->wMilliseconds;
	diff += (pst1->wSecond - pst0->wSecond)*1000;
	diff += (pst1->wMinute - pst0->wMinute)*60*1000;
	diff += (pst1->wHour - pst0->wHour)*60*60*1000;
	diff += (pst1->wHour - pst0->wHour)*60*60*1000;
	diff += (pst1->wDay - pst0->wDay)*24*60*60*1000;	
	diff += (pst1->wMonth - pst0->wMonth)*30*24*60*60*1000;	
	diff += (pst1->wYear - pst0->wYear)*365*24*60*60*1000;	
	return diff;
}

// Do a graceful shutdown of a the given socket sock.
BOOL DoGracefulShutdown(SOCKET sock)
{
    BOOL bRetVal = FALSE;
    WSAEVENT hEvent = WSA_INVALID_EVENT;
    long lNetworkEvents = 0;
    int status = 0;
    
    hEvent = WSACreateEvent();
    if (hEvent == WSA_INVALID_EVENT)
    {
        fprintf(stderr, "DoGracefulShutdown: WSACreateEvent failed: %d\n", 
                WSAGetLastError());
        goto CLEANUP;
    }

    DEBUG_OUT(("WSAEventSelect() start.\n"));
    lNetworkEvents = FD_CLOSE;
    if (WSAEventSelect(sock, hEvent, lNetworkEvents) != 0)
    {
        fprintf(stderr, "DoGracefulShutdown: WSAEventSelect failed: %d\n", 
                WSAGetLastError());
        goto CLEANUP;
    }
    DEBUG_OUT(("WSAEventSelect() finish.\n"));

    DEBUG_OUT(("shutdown() start.\n"));
    if (shutdown(sock, SD_SEND) != 0)
    {
        fprintf(stderr, "DoGracefulShutdown: shutdown failed: %d\n", 
                WSAGetLastError());
        goto CLEANUP;
    }
    DEBUG_OUT(("shutdown() finish.\n"));

    DEBUG_OUT(("WaitForSingleObject() start.\n"));
    if (WaitForSingleObject(hEvent, INFINITE) != WAIT_OBJECT_0)
    {
        fprintf(stderr, "DoGracefulShutdown: WaitForSingleObject failed: %d\n", 
                WSAGetLastError());
        goto CLEANUP;
    }
    DEBUG_OUT(("WaitForSingleObject() finish.\n"));

    do 
    {
        char buf[128];

	DEBUG_OUT(("recv() start.\n"));
        status = recv(sock, buf, sizeof(buf), 0);
	DEBUG_OUT(("recv() finish.\n"));
    } while (!(status == 0 || status == SOCKET_ERROR));

    DEBUG_OUT(("closesocket() start.\n"));
    if (closesocket(sock) != 0)
    {
        fprintf(stderr, "DoGracefulShutdown: closesocket failed: %d\n", 
                WSAGetLastError());
        goto CLEANUP;
    }
    DEBUG_OUT(("closesocket() finish.\n"));

    printf("Socket %d has been closed gracefully\n", sock);
    sock = INVALID_SOCKET;
    bRetVal = TRUE;
    
CLEANUP:

    if (hEvent != WSA_INVALID_EVENT)
    {
        WSACloseEvent(hEvent);
        hEvent = WSA_INVALID_EVENT;
    }

    if (sock != INVALID_SOCKET)
    {
        fprintf(stderr, "DoGracefulShutdown: Can't close socket gracefully. "
                        "So, closing it anyway ... \n");
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    
    return bRetVal;
}

#include <etc\user\getopt.c>
