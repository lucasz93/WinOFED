/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
 * Copyright (c) 2009 Intel Corp, Inc.  All rights reserved.
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
 *	Module Name: installsp.c
 *	Description: This module installs/removes the NetworkDirect provider for infiniband.
 *	execute:
 *	To install the service provider
 *		installsp -i
 *	To remove the service provider
 *		installsp -r
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2spi.h>
#include <stdio.h>
#include <string.h>
#include "..\..\..\etc\user\getopt.c"

#ifndef PFL_NETWORKDIRECT_PROVIDER
#define PFL_NETWORKDIRECT_PROVIDER          0x00000010
#endif

/* Initialize the LSP's provider path for Infiniband Service Provider dll */
#define IBAL_INDEX			0
#define IBAL_DEBUG_INDEX	1
#define WV_INDEX			2
#define WV_DEBUG_INDEX		3
#define MLX4_INDEX          4
#define MLX4v2_INDEX        5
#define MAX_INDEX			6

#define DEFAULT_PROVIDER_INDEX IBAL_INDEX

static int g_index=DEFAULT_PROVIDER_INDEX;

static const char * const provider_name[MAX_INDEX] = {
	"IBAL", "IBAL (debug)", "WinVerbs", "WinVerbs (debug)", "mlx4nd", "mlx4nd2"
};

static const WCHAR * const provider_path[MAX_INDEX] = {
	L"%SYSTEMROOT%\\system32\\ibndprov.dll",
	L"%SYSTEMROOT%\\system32\\ibndprov.dll",
	L"%SYSTEMROOT%\\system32\\wvndprov.dll",
	L"%SYSTEMROOT%\\system32\\wvndprovd.dll",
    L"%SYSTEMROOT%\\system32\\mlx4nd.dll",
    L"%SYSTEMROOT%\\system32\\mlx4nd.dll"
};

static const WCHAR * const provider_desc[MAX_INDEX] = {
	L"OpenFabrics Network Direct Provider",
	L"OpenFabrics Network Direct Provider (Debug)",
	L"OpenFabrics Winverbs Network Direct Provider",
	L"OpenFabrics Winverbs NetworkDirect Provider (Debug)",
    L"OpenFabrics NDv1 Provider for Mellanox ConnectX",
    L"OpenFabrics NDv2 Provider for Mellanox ConnectX"
};

static GUID provider_guid[MAX_INDEX] = {
	// {52CDAA00-29D0-46be-8FC6-E51D7075C338}
	{ 0x52CDAA00, 0x29D0, 0x46be, { 0x8f, 0xc6, 0xe5, 0x1d, 0x70, 0x75, 0xc3, 0x38 } },
	// {52CDAA00-29D0-46be-8FC6-E51D7075C338}
	{ 0x52CDAA00, 0x29D0, 0x46be, { 0x8f, 0xc6, 0xe5, 0x1d, 0x70, 0x75, 0xc3, 0x38 } },
	// {854DCE83-C872-4462-A3EB-C961C40E59D0}
	{ 0x854dce83, 0xc872, 0x4462, { 0xa3, 0xeb, 0xc9, 0x61, 0xc4, 0x0e, 0x59, 0xd0 } },
	// {1B8F1692-EDD9-4153-A159-605A73BCFFCF}
	{ 0x1b8f1692, 0xedd9, 0x4153, { 0xa1, 0x59, 0x60, 0x5a, 0x73, 0xbc, 0xff, 0xcf } },
    { 0xc42da8c6, 0xd1a1, 0x4f78, { 0x9f, 0x52, 0x27, 0x6c, 0x74, 0x93, 0x55, 0x96 } },
    { 0xb324ac22, 0x3a56, 0x4e6f, { 0xa9, 0xc4, 0x36, 0xdc, 0xc4, 0x28, 0xef, 0x65 } }
};

#ifdef _WIN64
#define WSCInstallProvider	WSCInstallProvider64_32
#endif	/* _WIN64 */

static void
show_usage (char *progname)
{
	char drv[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];

	_splitpath_s(progname,drv,_MAX_DRIVE, dir,_MAX_DIR, fname,_MAX_FNAME, ext,_MAX_EXT); // sad basename() equivalent.

	printf("usage: %s%s\n",fname,ext);
	printf("\tRemove, install and list OFA NetworkDirect providers\n\n");
	printf("\t[-[i|r] [provider]] Install/remove the specified/default"
			" provider\n");
	printf("\t[-d]                Install/remove debug version of provider\n");
	printf("\t[-l]                list OFA ND providers\n");
	printf("\t[-q]                Suppress default listing of providers\n");
	printf("\t[-h]                This text\n");
	printf("\tprovider must be one of the following names:\n");
	printf("\t\tibal\n");
	printf("\t\twinverbs\n");
    printf("\t\tmlx4nd\n");
    printf("\t\tmlx4nd2\n");
	printf("\t\t<blank> use the default ND provider '%s'\n",
		provider_name[DEFAULT_PROVIDER_INDEX]);
}


static void print_providers(void)
{
	WSAPROTOCOL_INFOW *protocol_info;
	unsigned int protocol_count;
	unsigned int i;
	DWORD protocol_size;
	INT err_no;
	int rc;

	/* Find the size of the buffer */
	protocol_size = 0;
	rc = WSCEnumProtocols (NULL, NULL, &protocol_size, &err_no);
	if (rc == SOCKET_ERROR && err_no != WSAENOBUFS) {
		printf("WSCEnumProtocols() returned error (%d)\n", err_no);
		return;
	}

	protocol_info = HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY, protocol_size);
	if (protocol_info == NULL) {
		printf("HeapAlloc() failed\n");
		return;
	}

	/* Enumerate the catalog for real */
	rc = WSCEnumProtocols (NULL, protocol_info, &protocol_size, &err_no);
	if (rc == SOCKET_ERROR) {
		printf("WSCEnumProtocols returned error for real enumeration (%d)\n",
			 err_no);
		HeapFree (GetProcessHeap (), 0, protocol_info);
		return;
	}

	protocol_count = rc;

	printf("\nCurrent providers:\n");
	for (i = 0; i < protocol_count; i++) {
		printf ("\t%010d - %S\n", protocol_info[i].dwCatalogEntryId,
				protocol_info[i].szProtocol);
	}

	HeapFree (GetProcessHeap (), 0, protocol_info);

	return;
}

/*
 * Function: install_provider
 *   Description: installs the service provider
 *
 * Note: most of the information setup here comes from "MSDN Home >
 * MSDN Library > Windows Development > Network Devices and
 * Protocols > Design Guide > System Area Networks > Windows Sockets
 * Direct > Windows Sockets Direct Component Operation > Installing
 * Windows Sockets Direct Components".
 * The direct link is http://msdn.microsoft.com/library/default.asp?url=/library/en-us/network/hh/network/wsdp_2xrb.asp
 */
static int install_provider(void)
{
	int rc, err_no;
	WSAPROTOCOL_INFOW provider;

	/* Setup the values in PROTOCOL_INFO */
	provider.dwServiceFlags1 = 
		XP1_GUARANTEED_DELIVERY | 
		XP1_GUARANTEED_ORDER | 
		XP1_MESSAGE_ORIENTED |
		XP1_CONNECT_DATA;  /*XP1_GRACEFUL_CLOSE;*/
	provider.dwServiceFlags2 = 0;	/* Reserved */
	provider.dwServiceFlags3 = 0;	/* Reserved */
	provider.dwServiceFlags4 = 0;	/* Reserved */
	provider.dwProviderFlags = PFL_HIDDEN | PFL_NETWORKDIRECT_PROVIDER;
	provider.ProviderId = provider_guid[g_index];
	provider.dwCatalogEntryId = 0;
	provider.ProtocolChain.ChainLen = 1;	/* Base Protocol Service Provider */
    if(g_index == MLX4v2_INDEX) {
        provider.iVersion = 0x20000/*ND_VERSION_2*/;
    } else {
		provider.iVersion = 1;
    }
	if (g_index == IBAL_INDEX || g_index == IBAL_DEBUG_INDEX) {
		provider.iAddressFamily = AF_INET;
		provider.iMaxSockAddr = sizeof(SOCKADDR_IN);
	} else {
		provider.iAddressFamily = AF_INET6;
		provider.iMaxSockAddr = sizeof(SOCKADDR_IN6);
	}
	provider.iMinSockAddr = 16;
	provider.iSocketType = -1;
	provider.iProtocol = 0;
	provider.iProtocolMaxOffset = 0;
	provider.iNetworkByteOrder = BIGENDIAN;
	provider.iSecurityScheme = SECURITY_PROTOCOL_NONE;
	provider.dwMessageSize = 0xFFFFFFFF; /* IB supports 32-bit lengths for data transfers on RC */
	provider.dwProviderReserved = 0;
	wcscpy_s( provider.szProtocol, WSAPROTOCOL_LEN+1, provider_desc[g_index] );

	printf("\nInstalling %s provider: ", provider_name[g_index]);
	rc = WSCInstallProvider(
		&provider_guid[g_index], provider_path[g_index], &provider, 1, &err_no );
	if( rc == SOCKET_ERROR )
	{
		if( err_no == WSANO_RECOVERY )
			printf("already installed\n");
		else
			printf("WSCInstallProvider failed: %d\n", err_no);
			rc = err_no;
	} else {
		printf("successful\n");
		rc = 0;
	}
	return rc;
}

/*
 * Function: remove_provider
 *   Description: removes our provider.
 */
static int remove_provider(void)
{
	int rc=0, err_no, rc1;

	/* Remove from the catalog */
	printf("\nRemoving %s provider: ", provider_name[g_index]);
	rc1 = WSCDeinstallProvider(&provider_guid[g_index], &err_no);
	if (rc1 == SOCKET_ERROR) {
		printf ("WSCDeinstallProvider failed: %d\n", err_no);
		rc1 = err_no;
	} else {
		printf ("successful\n");
		rc1 = 0;
	}

#ifdef _WIN64
	/* Remove from the 32-bit catalog too! */
	printf("Removing 32-bit %s provider: ", provider_name[g_index]);
	rc = WSCDeinstallProvider32(&provider_guid[g_index], &err_no);
	if (rc == SOCKET_ERROR) {
		printf ("WSCDeinstallProvider32 failed: %d\n", err_no);
		rc = err_no;
	} else {
		printf ("successful\n");
		rc = 0;
	}
#endif	/* _WIN64 */
	if ( rc || rc1 )
		return (rc ? rc : rc1);
	return 0;
}

static int get_prov_index(char *name)
{
	int i;

	if ( !name )
		return 0; /* assumes global 'g_index' set to default provider index. */

	for (i = 0; i < MAX_INDEX; i++) {
		if (!_stricmp(provider_name[i], name)) {
			g_index = i;
			return 0;
		}
	}
	return -1;
}

int __cdecl main (int argc, char *argv[])
{
	int ret=0, op;
	int install = 0, remove = 0, debug = 0, quiet = 0;
	WSADATA wsd;
	char *prov;

	while ((op = getopt(argc, argv, "?hi::r::dlq")) != -1) {
		switch (op) {
		case '?':
		case 'h':
				goto usage;
		case 'i':
			if (install || remove || get_prov_index(optarg)) {
				goto usage;
			}
			install = 1;
			break;
		case 'r':
			if (install || remove || get_prov_index(optarg)) {
				goto usage;
			}
			remove = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'l':
			quiet = 0;
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			goto usage;
		}
	}

	g_index += debug;

	if (WSAStartup (MAKEWORD (2, 2), &wsd) != 0) {
		printf ("InstallSP: Unable to load Winsock: %d\n", GetLastError ());
		return -1;
	}

	if (LOBYTE (wsd.wVersion) != 2 || HIBYTE (wsd.wVersion) != 2) {
		WSACleanup ();
		printf("InstallSP: Unable to find a usable version of Winsock DLL\n");
		ret = -1;
		goto exit;
	}

	if (install) {
		ret = install_provider();
	} else if (remove) {
		ret = remove_provider();
	}

	if (quiet == 0)
		print_providers();

exit:
	WSACleanup();
	return ret;

usage:
	show_usage(argv[0]);
	return -1;
}
