/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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
 *	Description: This module installs/removes a winsock service provider for infiniband. 
 *	execute:
 *	To install the service provider
 *		installsp -i	    
 *	To remove the service provider
 *		installsp -r
 */

#include <winsock2.h>
#include <ws2spi.h>
#include <stdio.h>
#include <stdlib.h>


/* Initialize the LSP's provider path for Infiband Service Provider dll */
static const WCHAR provider_path[] = L"%SYSTEMROOT%\\system32\\ibwsd.dll";
static const WCHAR provider_prefix[] =L" Winsock Direct for InfiniBand"; //includes one whitespace
static const char provider_name[] = VER_PROVIDER ;//L"%VER_PROVIDER% Winsock Direct for InfiniBand"; //(VER_PROVIDER ## WINDIR);
static const char winsock_key_path[] =
	"System\\CurrentControlSet\\Services\\Winsock\\Parameters\\TCP on SAN";
static const char openib_key_name[] = IB_COMPANYNAME;

/* Unique provider GUID generated with "uuidgen -s" */
static GUID provider_guid = {
	/* c943654d-2c84-4db7-af3e-fdf1c5322458 */
	0xc943654d, 0x2c84, 0x4db7,
	{ 0xaf, 0x3e, 0xfd, 0xf1, 0xc5, 0x32, 0x24,	0x58 }
};

#ifdef _WIN64
#define WSCInstallProvider	WSCInstallProvider64_32
#endif	/* _WIN64 */

#ifdef PERFMON_ENABLED
#include <Loadperf.h>
#include "wsd/ibsp_regpath.h"


typedef struct _pm_symbol_def
{
	DWORD	name_def;
	CHAR	name_str[40];
	CHAR	name_desc[40];
	CHAR	help_desc[256];

} pm_symbol_def_t;

static pm_symbol_def_t  _pm_symbols[]=
{
	{ IBSP_PM_OBJ,
	"IBSP_PM_OBJ",
	"IB Winsock Direct",
	"InfiniBand Windows Sockets Direct Provider."
	},
	{ IBSP_PM_COUNTER(BYTES_SEND),
	"IBSP_PM_BYTES_TX_SEC",
	"Send bytes/sec",
	"Send bytes/second, excluding RDMA Write."
	},
	{ IBSP_PM_COUNTER(BYTES_RECV),
	"IBSP_PM_BYTES_RX_SEC",
	"Recv bytes/sec",
	"Receive bytes/second, excluding RDMA Read."
	},
	{ IBSP_PM_COUNTER(BYTES_WRITE),
	"IBSP_PM_RDMA_WR_SEC",
	"RDMA Write bytes/sec",
	"RDMA Write bytes/second."
	},
	{ IBSP_PM_COUNTER(BYTES_READ),
	"IBSP_PM_RDMA_RD_SEC",
	"RDMA Read bytes/sec",
	"RDMA Read bytes/second."
	},
	{ IBSP_PM_COUNTER(BYTES_TOTAL),
	"IBSP_PM_BYTES_SEC",
	"Total bytes/sec",
	"Total bytes transmitted per second, including send, "
	"receive, RDMA Write, and RDMA Read."
	},
	{ IBSP_PM_COUNTER(COMP_SEND),
	"IBSP_PM_SEND_COMPLETIONS_SEC",
	"Send Completions/sec",
	"Send and RDMA Write Completions/sec."
	},
	{ IBSP_PM_COUNTER(COMP_RECV),
	"IBSP_PM_RECV_COMPLETIONS_SEC",
	"Recv Completions/sec",
	"Recv and RDMA Read Completions/sec."
	},
	{ IBSP_PM_COUNTER(COMP_TOTAL),
	"IBSP_PM_COMPLETIONS_SEC",
	"Total Completions/sec",
	"Total Completions processed per second."
	},
	{ IBSP_PM_COUNTER(INTR_TOTAL),
	"IBSP_PM_COMPLETIONS_INTR",
	"Total Interrupts/sec",
	"Completion Queue events per second."
	}
};

#define IBSP_PM_NUM_SYMBOLS  (sizeof(_pm_symbols)/sizeof(pm_symbol_def_t))
#define IBSP_PM_LANGUAGE "009" /* good for English */

static CHAR *
_IBSPGenerateFileName(char *header,	char *file )
{
	DWORD size1, size,full_file_name_size = 0;
	CHAR *full_file_name;
	int header_len = header == NULL ? 0 : strlen(header);

	size = GetTempPath(0, NULL);
	if (size == 0)
	{
		fprintf( stderr, "GetTempPath  failed\n" );
		return NULL;
	}
	full_file_name_size = size + strlen(file) + header_len;
	full_file_name = HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY, full_file_name_size);
	if ( full_file_name == NULL )
	{
		fprintf( stderr, "GetTempPath  failed\n" );
		return NULL;
	}
	size1 = GetTempPath(size, full_file_name + header_len);
	if (size != size1 + 1) 
	{
		fprintf( stderr, "Very strange, GetTempPath  returned something different\n" );
		HeapFree (GetProcessHeap (), 0, full_file_name);
		return NULL;
	}
	if (header_len != 0)
	{
		memcpy(full_file_name, header, header_len);
	}
	strcat_s(full_file_name, full_file_name_size , file);
	return full_file_name;
}


static DWORD
_IBSPPerfmonIniFilesGenerate( void )
{
	FILE	*f_handle;
	DWORD	num;
	DWORD ret = ERROR_SUCCESS;
	char *ibsp_pm_sym_file = NULL;
	char *ibsp_pm_ini_file = NULL;
    errno_t err;

	/* create ".h" file first */
	ibsp_pm_sym_file = _IBSPGenerateFileName(NULL, IBSP_PM_SYM_H_FILE);
	if( !ibsp_pm_sym_file )
	{
		fprintf( stderr, "_IBSPGenerateFileName  failed\n" );
		ret = ERROR_NOT_ENOUGH_MEMORY;
		goto Cleanup;
	}

	err = fopen_s(&f_handle, ibsp_pm_sym_file, "w+" );

	if( err != 0)
	{
		fprintf( stderr, "Create Header file %s failed\n", ibsp_pm_sym_file );
		ret = ERROR_FILE_INVALID;
		goto Cleanup;
	}

	fprintf(
		f_handle, "/* %s Generated by program */ \r\n", ibsp_pm_sym_file );
		
	
	for( num = 0; num < IBSP_PM_NUM_SYMBOLS; num++ )
	{
		fprintf( f_handle, "#define\t%s\t%d\r\n",
			_pm_symbols[num].name_str, _pm_symbols[num].name_def );
	}

	fflush( f_handle );
	fclose( f_handle );

	/* create 'ini' file next */
	ibsp_pm_ini_file = _IBSPGenerateFileName(NULL, IBSP_PM_INI_FILE);
	if( !ibsp_pm_sym_file )
	{
		fprintf( stderr, "_IBSPGenerateFileName  failed\n" );
		ret = ERROR_NOT_ENOUGH_MEMORY;
		goto Cleanup;
	}
	err = fopen_s(&f_handle, ibsp_pm_ini_file, "w+" );

	if( err != 0)
	{
		fprintf( stderr, "Create INI file %s  failed\n", ibsp_pm_ini_file );
		ret = ERROR_FILE_INVALID;
		goto Cleanup;
	}
	
	fprintf( f_handle, "[info]\r\ndrivername=" IBSP_PM_SUBKEY_NAME
		"\r\nsymbolfile=%s\r\n\r\n", ibsp_pm_sym_file );
	fprintf( f_handle,"[languages]\r\n" IBSP_PM_LANGUAGE
		"=language" IBSP_PM_LANGUAGE "\r\n\r\n" );

	fprintf( f_handle, 
		"[objects]\r\n%s_" IBSP_PM_LANGUAGE "_NAME=%s\r\n\r\n[text]\r\n",
		_pm_symbols[0].name_str, _pm_symbols[0].name_desc );
	
	for( num = 0; num < IBSP_PM_NUM_SYMBOLS; num++ )
	{
		fprintf( f_handle,"%s_" IBSP_PM_LANGUAGE "_NAME=%s\r\n",
			_pm_symbols[num].name_str, _pm_symbols[num].name_desc );
		fprintf( f_handle,"%s_" IBSP_PM_LANGUAGE "_HELP=%s\r\n",
			_pm_symbols[num].name_str, _pm_symbols[num].help_desc );
	}

	fflush( f_handle );
	fclose( f_handle );

Cleanup:
	if ( ibsp_pm_sym_file )
	{
		HeapFree (GetProcessHeap (), 0, ibsp_pm_sym_file);
	}
	if ( ibsp_pm_ini_file )
	{
		HeapFree (GetProcessHeap (), 0, ibsp_pm_ini_file);
	}
	return ret;
}


static void
_IBSPPerfmonIniFilesRemove( void )
{
	char *ibsp_pm_sym_file = NULL;
	char *ibsp_pm_ini_file = NULL;

	ibsp_pm_sym_file = _IBSPGenerateFileName(NULL, IBSP_PM_SYM_H_FILE);
	if( !ibsp_pm_sym_file )
	{
		fprintf( stderr, "_IBSPGenerateFileName  failed\n" );
		goto Cleanup;
	}

	ibsp_pm_ini_file = _IBSPGenerateFileName(NULL, IBSP_PM_INI_FILE);
	if( !ibsp_pm_sym_file )
	{
		fprintf( stderr, "_IBSPGenerateFileName  failed\n" );
		goto Cleanup;
	}

	if( !DeleteFile( ibsp_pm_ini_file ) )
	{
		fprintf( stderr, "Delete file %s failed status %d\n",
			ibsp_pm_ini_file, GetLastError() );
	}
	if( !DeleteFile( ibsp_pm_sym_file ) )
	{
		fprintf( stderr,"Delete file %s failed status %d\n",
			ibsp_pm_sym_file, GetLastError() );
	}

Cleanup:	
	if ( ibsp_pm_sym_file )
	{
		HeapFree (GetProcessHeap (), 0, ibsp_pm_sym_file);
	}
	if ( ibsp_pm_ini_file )
	{
		HeapFree (GetProcessHeap (), 0, ibsp_pm_ini_file);
	}
	
}


/* Try to create IB WSD Performance Register Keys */
static LONG
_IBSPPerfmonRegisterKeys( void )
{
	LONG	reg_status;
	HKEY	pm_hkey;
	DWORD	typesSupp = 7;

	reg_status = RegCreateKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_REGISTRY_PATH IBSP_PM_SUBKEY_PERF, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &pm_hkey, NULL );

	if( reg_status != ERROR_SUCCESS )
	{
		fprintf( stderr,
			"_IBSPPerfmonRegisterKeys Create Key %s failed with %d\n",
			IBSP_PM_REGISTRY_PATH IBSP_PM_SUBKEY_PERF, reg_status );
		return reg_status;
	}

	/* create/assign values to the key */
	RegSetValueExW( pm_hkey, L"Library", 0, REG_EXPAND_SZ,
		(LPBYTE)provider_path, sizeof(provider_path) );

	RegSetValueEx( pm_hkey, TEXT("Open"), 0, REG_SZ,
		(LPBYTE)TEXT("IBSPPmOpen"), sizeof(TEXT("IBSPPmOpen")) );

	RegSetValueEx( pm_hkey, TEXT("Collect"), 0, REG_SZ,
		(LPBYTE)TEXT("IBSPPmCollectData"), sizeof(TEXT("IBSPPmCollectData")) );

	RegSetValueEx( pm_hkey, TEXT("Close"), 0, REG_SZ,
		(LPBYTE)TEXT("IBSPPmClose"), sizeof(TEXT("IBSPPmClose")) );

	RegFlushKey( pm_hkey );
	RegCloseKey( pm_hkey );

	reg_status = RegCreateKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_EVENTLOG_PATH, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &pm_hkey, NULL );

	if( reg_status != ERROR_SUCCESS )
	{
		fprintf(stderr, "Create EventLog Key failed with %d\n", reg_status );
		return reg_status;
	}

	/* create/assign values to the key */
	RegSetValueExW( pm_hkey, L"EventMessageFile", 0, REG_EXPAND_SZ,\
		(LPBYTE)provider_path, sizeof(provider_path) );

	RegSetValueEx( pm_hkey, TEXT("TypesSupported"), 0, REG_DWORD,
		(LPBYTE)&typesSupp, sizeof(typesSupp) );

	RegFlushKey( pm_hkey );
	RegCloseKey( pm_hkey );

	return reg_status;
}


/* Try to destroy IB WSD Performance Register Keys */
static LONG
_IBSPPerfmonDeregisterKeys( void )
{
	LONG	reg_status;

	reg_status = RegDeleteKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_REGISTRY_PATH IBSP_PM_SUBKEY_PERF,
		(KEY_WOW64_32KEY | KEY_WOW64_64KEY), 0 );

	if( reg_status != ERROR_SUCCESS )
	{
		fprintf( stderr,
			"_IBSPPerfmonRegisterKeys Remove SubKey failed with %d\n",
			GetLastError() );
	}

	reg_status = RegDeleteKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_REGISTRY_PATH, (KEY_WOW64_32KEY | KEY_WOW64_64KEY), 0 );

	if( reg_status != ERROR_SUCCESS )
	{
		fprintf( stderr,
			"_IBSPPerfmonRegisterKeys Remove SubKey failed with %d\n",
			GetLastError() );
	}

	reg_status = RegDeleteKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_EVENTLOG_PATH, (KEY_WOW64_32KEY | KEY_WOW64_64KEY), 0 );

	if( reg_status != ERROR_SUCCESS )
	{
		fprintf( stderr,
			"_IBSPPerfmonRegisterKeys Remove SubKey failed with %d\n",
			GetLastError() );
	}

	return reg_status;
}


/*
 * functions will try to register performance counters
 * definitions with PerfMon application.
 * API externally called by lodctr.exe/unlodctr.exe utilities.
 */
static DWORD
_IBSPPerfmonRegisterCounters( void )
{
	DWORD status;
	char *ibsp_pm_ini_file = NULL;

	ibsp_pm_ini_file = _IBSPGenerateFileName("unused ", IBSP_PM_INI_FILE);
	if( !ibsp_pm_ini_file )
	{
		fprintf( stderr, "_IBSPGenerateFileName  failed\n" );
		status = ERROR_NOT_ENOUGH_MEMORY;
		goto Cleanup;
	}

	/*
	 * format commandline string, as per SDK :
	 *	Pointer to a null-terminated string that consists of one or more 
	 *	arbitrary letters, a space, and then the name of the initialization
	 *	file.
	 */
	status = LoadPerfCounterTextStrings( ibsp_pm_ini_file, TRUE );
	if( status != ERROR_SUCCESS )
	{
		status = GetLastError();
		fprintf( stderr,
			"IBSPPerfmonRegisterCounters install failed status %d\n", status );
	}
Cleanup:	
	if ( ibsp_pm_ini_file )
	{
		HeapFree (GetProcessHeap (), 0, ibsp_pm_ini_file);
	}

	return status;
}


/*
 * functions will try to unregister performance counters
 * definitions with PerfMon application.
 * API externally called by lodctr.exe/unlodctr.exe utilities.
 */
static DWORD
_IBSPPerfmonDeregisterCounters( void )
{
	DWORD status;

	/*
	 * format commandline string, as per SDK :
	 *	Pointer to a null-terminated string that consists of one or more 
	 *	arbitrary letters, a space, and then the name of the initialization
	 *	file.
	 */
	status = UnloadPerfCounterTextStrings(
		TEXT("unused ") TEXT(IBSP_PM_SUBKEY_NAME), TRUE );
	if( status != ERROR_SUCCESS )
	{
		fprintf( stderr,
			"IBSPPerfmonDeregisterCounters remove failed status %d\n",
			status );
	}
	return status;
}

#endif /* PERFMON_ENABLED */


/*
 * Function: usage
 *   Description: Prints usage information.
 */
static void
usage (char *progname)
{
	printf ("usage: %s [-i/-r [-p]]\n", progname);
	printf ("    -i   Install the service provider\n"
			"    -r   Remove the %s service provider\n"
			"    -r <name>   Remove the specified service provider\n"
			"    -l   List service providers\n",VER_PROVIDER);
}


/* Function: print_providers
 *   Description: 
 *     This function prints out each entry in the Winsock catalog.
*/
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

	/* Allocate the buffer */
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

	for (i = 0; i < protocol_count; i++) {
		printf ("%010d - %S\n", protocol_info[i].dwCatalogEntryId,
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
static void install_provider(void)
{
	int rc;
	INT err_no;
	LONG reg_error;
	WSAPROTOCOL_INFOW provider;
	HKEY hkey;
    size_t res;
    size_t st_len;
    size_t char_converted;

	/* Now setup the key. */
	reg_error = RegCreateKeyExA( HKEY_LOCAL_MACHINE, winsock_key_path,
		0, NULL, REG_OPTION_NON_VOLATILE, (KEY_WRITE | KEY_READ), NULL,
		&hkey, NULL );
	if( reg_error == ERROR_SUCCESS )
	{
		reg_error = RegSetValueExA( hkey, openib_key_name, 0, REG_BINARY,
			(PBYTE)&provider_guid, sizeof(GUID) );
		if( reg_error == ERROR_SUCCESS )
		{
			/* Force the system to write the new key now. */
			RegFlushKey(hkey);
		}
		else
		{
			fprintf(stderr, "RegSetValueEx failed with %d\n", GetLastError());
		}

		RegCloseKey(hkey);
	}
	else
	{
		fprintf(stderr, "Could not get a handle on Winsock registry (%d)\n", GetLastError());
	}

	/* Setup the values in PROTOCOL_INFO */
	provider.dwServiceFlags1 = 
		XP1_GUARANTEED_DELIVERY | 
		XP1_GUARANTEED_ORDER | 
		XP1_MESSAGE_ORIENTED |
		XP1_GRACEFUL_CLOSE;
	provider.dwServiceFlags2 = 0;	/* Reserved */
	provider.dwServiceFlags3 = 0;	/* Reserved */
	provider.dwServiceFlags4 = 0;	/* Reserved */
	provider.dwProviderFlags = PFL_HIDDEN;
	provider.ProviderId = provider_guid;	/* Service Provider ID provided by vendor. Need to be changed later */
	provider.dwCatalogEntryId = 0;
	provider.ProtocolChain.ChainLen = 1;	/* Base Protocol Service Provider */
	provider.iVersion = 2;	/* don't know what it is */
	provider.iAddressFamily = AF_INET;
	provider.iMaxSockAddr = 16;
	provider.iMinSockAddr = 16;
	provider.iSocketType = SOCK_STREAM;
	provider.iProtocol = IPPROTO_TCP;
	provider.iProtocolMaxOffset = 0;
	provider.iNetworkByteOrder = BIGENDIAN;
	provider.iSecurityScheme = SECURITY_PROTOCOL_NONE;
	provider.dwMessageSize = 0xFFFFFFFF; /* IB supports 32-bit lengths for data transfers on RC */
	provider.dwProviderReserved = 0;

	st_len = strlen(provider_name);
	mbstowcs_s(&res, provider.szProtocol, (WSAPROTOCOL_LEN+1)/2, provider_name, st_len); //do not count \0
	// We can't use there mbstowcs_s 
	//rc = mbstowcs_s(&convertedChars, provider.szProtocol, sizeof(provider_name), provider_name, );
    if (res  != st_len) {
        printf("<install_provider> Can't convert string %s to WCHAR\n",provider_name);
        printf("Converted %d from %d\n", res, st_len);
    }
    wcscpy_s( provider.szProtocol + st_len, WSAPROTOCOL_LEN+1-st_len, provider_prefix);
    //wprintf(L"provider.szProtocol = %s\n",provider.szProtocol);

	rc = WSCInstallProvider(
		&provider_guid, provider_path, &provider, 1, &err_no );
	if( rc == SOCKET_ERROR )
	{
		if( err_no == WSANO_RECOVERY )
			printf("The provider is already installed\n");
		else
			printf("install_provider: WSCInstallProvider failed: %d\n", err_no);
	}
}

/*
 * Function: remove_provider
 *   Description: removes our provider.
 */
static void remove_provider( const char* const provider_name )
{
	int rc;
	int err_no;
	LONG reg_error;
	HKEY hkey;

	/* Remove our key */
	reg_error = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
							 winsock_key_path,
							 0,
							 (KEY_WRITE | KEY_READ),
							 &hkey);
	if (reg_error == ERROR_SUCCESS) {

		reg_error = RegDeleteValueA(hkey, provider_name);
		if (reg_error == ERROR_SUCCESS) {
			/* Force the system to remove the key now. */
			RegFlushKey(hkey);
		} else {
			fprintf(stderr, "RegDeleteValue failed with %d\n", GetLastError());
		}

		RegCloseKey(hkey);
	}

	/* Remove from the catalog */
	rc = WSCDeinstallProvider(&provider_guid, &err_no);
	if (rc == SOCKET_ERROR) {
		printf ("WSCDeinstallProvider failed: %d\n", err_no);
	}

#ifdef _WIN64
	/* Remove from the 32-bit catalog too! */
	rc = WSCDeinstallProvider32(&provider_guid, &err_no);
	if (rc == SOCKET_ERROR) {
		printf ("WSCDeinstallProvider32 failed: %d\n", err_no);
	}
#endif	/* _WIN64 */
}

/* Function: main
 *
 *  Description:
 *    Parse the command line arguments and call either the install or remove
 *    routine.
 */
int __cdecl main (int argc, char *argv[])
{
	WSADATA wsd;

	/* Load Winsock */
	if (WSAStartup (MAKEWORD (2, 2), &wsd) != 0) {
		printf ("InstallSP: Unable to load Winsock: %d\n", GetLastError ());
		return -1;
	}

	/* Confirm that the WinSock DLL supports 2.2. Note that if the
	 * DLL supports versions greater than 2.2 in addition to 2.2, it
	 * will still return 2.2 in wVersion since that is the version we
	 * requested. */
	if (LOBYTE (wsd.wVersion) != 2 || HIBYTE (wsd.wVersion) != 2) {

		/* Tell the user that we could not find a usable WinSock DLL. */
		WSACleanup ();
		printf
			("InstallSP: Unable to find a usable version of Winsock DLL\n");
		return -1;
	}
	if (argc < 2) {
		usage (argv[0]);
		return -1;
	}
	if ((strlen (argv[1]) != 2) && (argv[1][0] != '-')
		&& (argv[1][0] != '/')) {
		usage (argv[0]);
		return -1;
	}
	switch (tolower (argv[1][1])) {

	case 'i':
		/* Install the Infiniband Service Provider */
		install_provider ();
#ifdef PERFMON_ENABLED
		_IBSPPerfmonIniFilesGenerate();
		if ( _IBSPPerfmonRegisterKeys() == ERROR_SUCCESS )
				_IBSPPerfmonRegisterCounters();
#endif
		break;

	case 'r':
		/* Remove the service provider */
		if( argc == 2 )
			remove_provider( openib_key_name );
		else
			remove_provider( argv[2] );
#ifdef PERFMON_ENABLED
		_IBSPPerfmonIniFilesRemove();
		if ( _IBSPPerfmonDeregisterCounters() == ERROR_SUCCESS )
			_IBSPPerfmonDeregisterKeys();
#endif
		break;

	case 'l':
		/* List existing providers */
		print_providers();
		break;
	
	default:
		usage (argv[0]);
		break;
	}

	WSACleanup ();

	return 0;
}
