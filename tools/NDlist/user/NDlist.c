/* ts=4,noexpandtab */
/*
 * Copyright (c) 2013 Intel Corp, Inc.  All rights reserved.
 * Portions Copyright (c) 2013 Microsoft Corporation.  All rights reserved.
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
 */

/*
 *	Module Name: NDlist
 *	Description: list local NetworkDirect(v2) devices & IP address.
 */

#include <stdio.h>
#include <tchar.h>
#include <process.h>
#include <string.h>
#include <assert.h>

#include <ndsupport.h>
#include <ndstatus.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2spi.h>

#ifdef __cplusplus
#include <new>
#endif

#include <getopt.h>
#include "..\..\..\etc\user\getopt.c"


static BOOL gDetails;
static BOOL gIPv4_only;
static BOOL gIPv6_only;


/* Prototypes */
char * ND_error_str( HRESULT hr );


static void usage(void)
{
	fprintf(stderr,
		"usage: ndlist [-hd46]\n"
		"   where:\n"
		"      -d  Device details\n"
        "      -4  IPv4 addresses only\n"
        "      -6  IPv6 addresses only\n");
	exit(0);
}

char *
IPaddr_str(SOCKADDR *sa, OPTIONAL char *buf)
{
	int rc;
	static char lbuf[64];
	char *cp, *str = (buf ? buf : lbuf);
	DWORD sa_len, sa_buflen;

	sa_len = (sa->sa_family == AF_INET6
			? sizeof(struct sockaddr_in6)
			: sizeof(struct sockaddr_in));
	sa_buflen = sizeof(lbuf);
	rc = WSAAddressToString( sa, sa_len, NULL, str, &sa_buflen );
	if ( rc ) {
		fprintf(stderr,
			"%s() ERR: WSAAddressToString %#x sa_len %d\n",
				__FUNCTION__, WSAGetLastError(), sa_len);
	}
	return str;
}


static void display_ND_addr_list( SOCKET_ADDRESS_LIST *pSAL )
{
	HRESULT hr;
	SOCKET_ADDRESS *pSA;
	char *cp, buf[128];
	ND2_ADAPTER_INFO nd_adapter_info;
	int i;

	printf("NetworkDirect v2 devices: %d\n",pSAL->iAddressCount);

	for(i=0,pSA=pSAL->Address; i < pSAL->iAddressCount; i++,pSA++)
	{
		cp = buf;
		switch ( pSA->lpSockaddr->sa_family )
		{
		  case AF_INET:
			if( gIPv6_only)
				continue;
			cp += sprintf(buf,"  ND%d AF_INET  [%s]",
						i, IPaddr_str(pSA->lpSockaddr,0));
			break;

		  case AF_INET6:
			if( gIPv4_only)
				continue;
            cp += sprintf(cp,"  ND%d AF_INET6 [%s]",
						i,IPaddr_str(pSA->lpSockaddr,0) );
			break;

		  default: 
			fprintf(stderr, "%s() Unknown SA_Family %d",
				__FUNCTION__, pSA->lpSockaddr->sa_family);
			continue;
		}

		if( gDetails )
		{
			IND2Adapter *pAdapter;
			SIZE_T sa_len;
			ULONG Len;

			sa_len = pSA->iSockaddrLength;

			hr = NdOpenAdapter( &IID_IND2Adapter,
								(SOCKADDR*)pSA->lpSockaddr,
								sa_len,
								&pAdapter);
			if( FAILED(hr) )
			{
				fprintf(stderr, "FAIL: NdOpenIAdapter [%s] ERR: %s\n",
						IPaddr_str(pSA->lpSockaddr,0), ND_error_str(hr));
			}
			else
			{
				/* get adapter info V2 */
				nd_adapter_info.InfoVersion = ND_VERSION_2;
				Len = (ULONG) sizeof(ND2_ADAPTER_INFO);
				hr = pAdapter->lpVtbl->Query( pAdapter,
				      						  &nd_adapter_info,
				      						  &Len ); 
				if( FAILED( hr ) ) 
				{
					fprintf(stderr,
						"IND2Adapter::Query failed %s\n", ND_error_str(hr));
				}	
				pAdapter->lpVtbl->Release( pAdapter );	// close the ND adapter

				cp += sprintf(cp, " VendorID %#x DeviceID %#x",
									nd_adapter_info.VendorId,
									nd_adapter_info.DeviceId );
			}
		}
		printf("%s\n",buf);
		buf[0] = '\0';
    }
}

int __cdecl main(int argc, char **argv)
{
	int					c, i;
	HRESULT				hr;
	HANDLE				Heap=GetProcessHeap();
	ULONG				BufSize=0;
	SOCKET_ADDRESS_LIST	*pSAL;
	SOCKET_ADDRESS		*pSA;

	while( (c=getopt(argc,argv,"?hd46")) != EOF )
	{
	    switch( c )
		{
		  case '4':
			gIPv4_only++;
			break;
		  case '6':
			gIPv6_only++;
			break;
		  case 'd':
			gDetails++;
			break;
		  case 'h':
		  case '?':
			usage();
			break;
		  default:
			fprintf(stderr,"%s: unknown switch '%c'?\n",argv[0],c);
			return 1;
			break;
		}
	}

	hr = NdStartup();
	if( FAILED(hr) )
	{
		fprintf(stderr, "%s() NdStartup failed with %s\n",
				__FUNCTION__, ND_error_str(hr) );
		exit( __LINE__ );
	}

	/* Query to get required bufsize, BufSize updated in failure */
	hr = NdQueryAddressList( 0, NULL, &BufSize );

	pSAL = (SOCKET_ADDRESS_LIST*) HeapAlloc( Heap, 0, BufSize );
	if ( !pSAL ) {
		fprintf(stderr, "%s() malloc(%d) failed?\n", __FUNCTION__, BufSize);
		return 1;
	}

	hr = NdQueryAddressList( 0, pSAL, &BufSize );
	if ( FAILED(hr) ) {
		fprintf(stderr, "%s() NdQueryAddressList() ERR: %s\n",
				__FUNCTION__,ND_error_str(hr));
		HeapFree( Heap, 0, (void*)pSAL );
		return 2;
	}

	display_ND_addr_list( pSAL );

	HeapFree( Heap, 0, (void*)pSAL );

	hr = NdCleanup();
	if( FAILED(hr) )
	{
		fprintf(stderr, "%s() NdCleanup failed %s\n",
			__FUNCTION__, ND_error_str(hr));
	}

	return 0;
}


// Retrieve the system error message for the last-error code

char *
GetLastError_str( HRESULT hr, char *errmsg, SIZE_T max_msg_len )
{

	LPVOID lpMsgBuf;
	//DWORD dw = GetLastError(); 
	DWORD dw = (DWORD)hr;
	errno_t rc;

	FormatMessage(
        	FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        	FORMAT_MESSAGE_FROM_SYSTEM |
        	FORMAT_MESSAGE_IGNORE_INSERTS,
        	NULL,
        	dw,
        	MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        	(LPTSTR) &lpMsgBuf,
        	0, NULL );

	strcpy_s(errmsg,max_msg_len,"NTStatus: ");
	rc = strncat_s( errmsg, max_msg_len-strlen(errmsg), lpMsgBuf, _TRUNCATE);

	LocalFree(lpMsgBuf);

	return errmsg;
}


#define NDERR(a) 	\
	case a:		\
	  em = #a ;	\
	  break

#define ND_FLUSHED 0x10000L	/* undocumented ND error code */
#define ND_DISCONNECTED 0xc000020C 

char *
ND_error_str( HRESULT hr )
{
	static char lerr[128];
	char *em=NULL;

	switch( hr )
	{
	    NDERR(ND_SUCCESS);
	    NDERR(ND_FLUSHED);
	    NDERR(ND_TIMEOUT);
	    NDERR(ND_PENDING);
	    NDERR(ND_BUFFER_OVERFLOW);
	    NDERR(ND_DEVICE_BUSY);
	    NDERR(ND_NO_MORE_ENTRIES);
	    NDERR(ND_UNSUCCESSFUL);
	    NDERR(ND_ACCESS_VIOLATION);
	    NDERR(ND_INVALID_HANDLE);
	    NDERR(ND_INVALID_DEVICE_REQUEST);
	    NDERR(ND_INVALID_PARAMETER);
	    NDERR(ND_NO_MEMORY);
	    NDERR(ND_INVALID_PARAMETER_MIX);
	    NDERR(ND_DATA_OVERRUN);
	    NDERR(ND_SHARING_VIOLATION);
	    NDERR(ND_INSUFFICIENT_RESOURCES);
	    NDERR(ND_DEVICE_NOT_READY);
	    NDERR(ND_IO_TIMEOUT);
	    NDERR(ND_NOT_SUPPORTED);
	    NDERR(ND_INTERNAL_ERROR);
	    NDERR(ND_INVALID_PARAMETER_1);
	    NDERR(ND_INVALID_PARAMETER_2);
	    NDERR(ND_INVALID_PARAMETER_3);
	    NDERR(ND_INVALID_PARAMETER_4);
	    NDERR(ND_INVALID_PARAMETER_5);
	    NDERR(ND_INVALID_PARAMETER_6);
	    NDERR(ND_INVALID_PARAMETER_7);
	    NDERR(ND_INVALID_PARAMETER_8);
	    NDERR(ND_INVALID_PARAMETER_9);
	    NDERR(ND_INVALID_PARAMETER_10);
	    NDERR(ND_CANCELED);
	    NDERR(ND_REMOTE_ERROR);
	    NDERR(ND_INVALID_ADDRESS);
	    NDERR(ND_INVALID_DEVICE_STATE);
	    NDERR(ND_INVALID_BUFFER_SIZE);
	    NDERR(ND_TOO_MANY_ADDRESSES);
	    NDERR(ND_ADDRESS_ALREADY_EXISTS);
	    NDERR(ND_CONNECTION_REFUSED);
	    NDERR(ND_CONNECTION_INVALID);
	    NDERR(ND_CONNECTION_ACTIVE);
	    NDERR(ND_HOST_UNREACHABLE);
	    NDERR(ND_CONNECTION_ABORTED);
	    NDERR(ND_DEVICE_REMOVED);
	    NDERR(ND_DISCONNECTED);

	  default:
		em = GetLastError_str( hr, lerr, sizeof(lerr) );
		if ( em == NULL ) {
			_snprintf(lerr,sizeof(lerr),"ND?? Unknown ND error %#08x",hr);
			em = lerr;
		}
		break;
	}
	return em;
}
#undef NDERR

