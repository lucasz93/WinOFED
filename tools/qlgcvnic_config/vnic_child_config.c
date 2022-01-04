/*
 * Copyright (c) 2009 QLogic Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenFabrics.org BSD license
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

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <al_dev.h>
#include <complib/cl_byteswap.h>
#include <complib/cl_memory.h>

#define MAX_DEVICE_ID_LEN     200
#define MAX_DEVICE_STRING_LEN 		MAX_DEVICE_ID_LEN + 2	/* add extra 4 bytes in case we need double NULL ending */

#pragma pack(push, 1)
typedef struct _ca_ioc_info {
	net64_t					ca_guid;
	ib_ioc_info_t				info;
	ib_svc_entry_t				svc_entry_array[1];
} ca_ioc_info_t;

typedef struct _child_device_info {
	wchar_t		device_id[MAX_DEVICE_STRING_LEN];  
	uint32_t	device_id_size;
	wchar_t		compatible_id[MAX_DEVICE_STRING_LEN];
	uint32_t	compatible_id_size;
	wchar_t		hardware_id[MAX_DEVICE_STRING_LEN];
	uint32_t	hardware_id_size;
	wchar_t		description[MAX_DEVICE_STRING_LEN];
	uint32_t	description_size;
	ca_ioc_info_t	ca_ioc_path;
	uint32_t	uniqueinstanceid;
}  child_device_info_t;

#pragma pack(pop)

int usage()
{
	printf( "Correct usage to create VNIC child devices is:-\n" );
	printf( "qlgcvnic_config -c <CAGUID> <IOCGUID> "
		"<InstanceID> <Interface Description>\n" );
	printf( "Executing qlgcvnic_config without any option or with -l "
		"option will list the IOCs reachable from the host\n" );
	return -1;
}

DWORD	send_device_ioctl( DWORD ioctlcode, PVOID input_buf, DWORD in_size, PVOID *output_buf, PDWORD out_size )
{
	DWORD	dwRet = 0, dwMemSize, dwBytesRet = 0;
	BOOL	bRet;
	PVOID	pBuf;
	HANDLE	hDevice = INVALID_HANDLE_VALUE;

	hDevice = CreateFileW ( L"\\\\.\\VNICCONFIG",
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,	/* share mode none */
				NULL,					/* no security */
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				NULL );					/* no template; */

	if ( hDevice == INVALID_HANDLE_VALUE ) 
	{
		printf( "Error opening VNICCONFIG device file\n" );
		return ERROR_FILE_NOT_FOUND;
	}

	switch( ioctlcode ) 
	{
		case 	UAL_IOC_DEVICE_CREATE:
			bRet = DeviceIoControl( hDevice, ioctlcode, input_buf, in_size, NULL, 0, &dwBytesRet, NULL );

			if ( !bRet ) 
			{
				dwRet = GetLastError();
				printf( "GetLastError after UAL_IOC_DEVICE_CREATE gives %d\n", dwRet );
			}
			break;

		case	UAL_IOC_LIST:
			dwMemSize = sizeof(ca_ioc_info_t ) * 2;
			pBuf = NULL;

			do 
			{
				if ( pBuf ) 
				{
					cl_free( pBuf );
					dwMemSize *= 2;					
					pBuf = NULL;
				}

				pBuf = cl_malloc( dwMemSize );

				if ( !pBuf ) 
				{
					printf( "Insufficient memory\n" );
					dwRet = ERROR_NOT_ENOUGH_MEMORY;
					break;
				}

				bRet = DeviceIoControl( hDevice, ioctlcode, NULL, 0, pBuf, dwMemSize, &dwBytesRet, NULL );
				dwRet = GetLastError();

				if ( bRet ) 
				{
					dwRet = 0;
					break;
				}
				
			}while( dwRet == ERROR_INSUFFICIENT_BUFFER );

			*output_buf = pBuf;
			*out_size = dwBytesRet;

			break;
	}

	CloseHandle( hDevice );
	return dwRet;
}

ca_ioc_info_t	*find_ca_ioc_path ( ca_ioc_info_t	*pList, DWORD	nListSize,
			uint64_t	ca_guid, uint64_t	ioc_guid )
{
	while ( nListSize ) 
	{

		if ( pList->ca_guid == ca_guid && pList->info.profile.ioc_guid == ioc_guid ) 
		{
			return pList;
		}
		nListSize--;
		pList++;
	}

	return NULL;
}

#define	IBIOU_SERVICE_PARAM_KEY		"SYSTEM\\CurrentControlSet\\Services\\ibiou"
#define	IBIOU_PARAM_KEY_DEVICE_VALUE	"StaticChild"
#define	IBIOU_PARAMETER_NAME		"Parameters"
#define	MAX_VALUE_LENGTH		2048

DWORD	write_device_name( char	*device_name )
{

	DWORD	dwRet = 0, dwBytesRet = 0, n_size = 100, dwType;
	HKEY	hKey = INVALID_HANDLE_VALUE;
	LONG	lRet = -1;
	PVOID	pOldValue, pNewValue, pBuf;
	char	szKeyName[200];

	sprintf( szKeyName, "%s\\%s", IBIOU_SERVICE_PARAM_KEY, IBIOU_PARAMETER_NAME );
	lRet = RegOpenKeyExA( HKEY_LOCAL_MACHINE, szKeyName, 0, KEY_ALL_ACCESS, 
				&hKey );


	if ( lRet != ERROR_SUCCESS ) 
	{
		printf( "Opening services key of ibiou failed\n" );
		dwRet = -1;
	} 
	else 
	{
		do 
		{
			pOldValue = cl_malloc( n_size );

			if ( !pOldValue ) 
			{
				printf( "Not enough memory on system\n" );
				break;
			}
			dwType = REG_MULTI_SZ;

			lRet = RegQueryValueExA( hKey, IBIOU_PARAM_KEY_DEVICE_VALUE, 0, &dwType, pOldValue, &dwBytesRet );


			if ( lRet == ERROR_MORE_DATA ) 
			{
				cl_free( pOldValue );
				pOldValue = NULL;
				n_size = n_size*2;
			}

		} while( lRet == ERROR_MORE_DATA && n_size < MAX_VALUE_LENGTH );

		if ( lRet != ERROR_SUCCESS ) 
		{
			cl_free( pOldValue );
			pOldValue = NULL;
		}


		n_size = dwBytesRet + strlen( device_name )*sizeof( char ) + 2*sizeof( char ); /*  more for NULLs */
		pNewValue = cl_malloc( n_size );
		pBuf = pNewValue;
		cl_memset( pNewValue, 0x00, n_size );
		if ( !pNewValue ) 
		{
			printf( "Not enough memory on system\n" );
		} 
		else 
		{
			if ( pOldValue ) 
			{	/* Come here only when "StaticChild" key was there already*/
				cl_memcpy( pBuf, pOldValue, dwBytesRet );

				while ( *( ( PBYTE )pBuf ) != 0 || *( ( PBYTE ) pBuf + 1 ) != 0 )
					pBuf = ( PBYTE ) pBuf + 1;

				pBuf = ( PBYTE ) pBuf + 1;
				cl_free( pOldValue );
				pOldValue = NULL;
			}

			cl_memcpy( pBuf, device_name, strlen( device_name ) );
			lRet = RegSetValueExA( hKey, IBIOU_PARAM_KEY_DEVICE_VALUE, 0, REG_MULTI_SZ, 
						pNewValue, ( strlen( device_name ) + dwBytesRet + 2 ) ); /* Two bytes for extra NULLs*/
			cl_free( pNewValue );
			RegCloseKey( hKey );
			if ( lRet != ERROR_SUCCESS ) 
			{
				printf( "Error setting device name in value of services key of ibiou\n" );
				dwRet = -1;
			}
		}

	}
	return dwRet;	
}

static const	char 	*value_names[] = {
		"CAGUID",
#define		CAGUID_INDEX		0
		"IOCGUID",
#define		IOCGUID_INDEX		1
		"CompatibleId",
#define		COMPATIBLEID_INDEX	2
		"Description",
#define		DESCRIPTION_INDEX	3
		"DeviceId",
#define		DEVICEID_INDEX		4
		"HardwareId",
#define		HARDWAREID_INDEX	5
		"InstanceId"
#define		INSTANCEID_INDEX	6
#define		MAXVALUE_INDEX		7
};

DWORD	write_deviceinfo_to_registry( uint64_t	ca_guid, uint64_t	ioc_guid, 
					uint32_t	instance_id, char *device_name )
{
	DWORD	dwRet = 0, dwDisposition, dwType, dwSize;
	HKEY	hKey = INVALID_HANDLE_VALUE;
	char	szKeyName[250];
	LONG	lRet;
	DWORD	dwIndex;
	PVOID	pBuf;

	dwRet = write_device_name( device_name );

	if ( dwRet == 0 ) 
	{
		sprintf( szKeyName, "%s\\%s\\%s", 
			IBIOU_SERVICE_PARAM_KEY, IBIOU_PARAMETER_NAME, device_name );
		lRet = RegCreateKeyExA( HKEY_LOCAL_MACHINE, szKeyName, 0,
					NULL, REG_OPTION_NON_VOLATILE, 
					KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition  );

		if ( dwDisposition == REG_CREATED_NEW_KEY ) 
		{
			dwIndex = CAGUID_INDEX;
			
			while ( dwIndex < MAXVALUE_INDEX ) 
			{

				switch( dwIndex ) 
				{
					case	CAGUID_INDEX:
						pBuf = &ca_guid;
						dwType = REG_BINARY;
						dwSize = sizeof( uint64_t );
						break;

					case	IOCGUID_INDEX:
						pBuf = &ioc_guid;
						dwType = REG_BINARY;
						dwSize = sizeof( uint64_t );						
						break;

					case	DEVICEID_INDEX:
						pBuf = "IBA\\qlgcvnic";
						dwType = REG_SZ;
						dwSize = strlen( pBuf ) + 1;
						break;

					case	COMPATIBLEID_INDEX:		/* Currently all id's used are same. */
					case	HARDWAREID_INDEX:
						pBuf = "IBA\\qlgcvnic";
						dwType = REG_MULTI_SZ;
						dwSize = strlen( pBuf ) + 1;						
						break;

					case	DESCRIPTION_INDEX:	
						pBuf = device_name;
						dwType = REG_SZ;
						dwSize = strlen( pBuf ) + 1;						
						break;

					case	INSTANCEID_INDEX:
						pBuf = &instance_id;
						dwType = REG_DWORD;
						dwSize = sizeof( instance_id );
						break;

					default:
						break;
				}
				lRet = RegSetValueExA( hKey, value_names[dwIndex], 0,
							dwType, pBuf, dwSize );

				if ( lRet != ERROR_SUCCESS ) 
				{
					printf( "Failed in setting device information as Parameters of ibiou driver\n" );
					dwRet = -1;
					break;
				}

				dwIndex++;
			}
		}

		if ( hKey != INVALID_HANDLE_VALUE ) 
		{
			RegCloseKey( hKey );
		}
	}

	return dwRet;
}

DWORD	create_child_device( uint64_t	ca_guid, uint64_t	ioc_guid, uint32_t instance_id, char *device_name )
{
	ca_ioc_info_t		*pList = NULL, *p_ca_ioc_entry = NULL;
	DWORD			dwRet = 0, dwBytesRet;
	child_device_info_t	*child_dev = NULL;
	WCHAR			szDevName[200];


	dwRet = send_device_ioctl( UAL_IOC_LIST, NULL, 0, &pList, &dwBytesRet );

	if ( dwRet ) 
	{
		printf( "Obtaining IOC LIST from ibiou failed\n" );
	} 
	else 
	{
		p_ca_ioc_entry = find_ca_ioc_path( pList, dwBytesRet/sizeof( ca_ioc_info_t ),
					ca_guid, ioc_guid );

		if ( p_ca_ioc_entry ) 
		{
			child_dev = cl_malloc( sizeof( child_device_info_t ) );
			cl_memset( child_dev, 0x00, sizeof( child_device_info_t ) );

			if ( !child_dev ) 
			{
				printf( "Allocating memory for child device failed\n" );
			} 
			else 
			{
				child_dev->ca_ioc_path = *p_ca_ioc_entry;
				wcscpy( child_dev->device_id, L"IBA\\qlgcvnic" );
				child_dev->device_id_size = ( wcslen( child_dev->device_id ) + 1 )*sizeof( WCHAR );
				wcscpy( child_dev->hardware_id, L"IBA\\qlgcvnic" );
				child_dev->hardware_id_size = ( wcslen( child_dev->hardware_id ) + 2 )*sizeof( WCHAR );
				wcscpy( child_dev->compatible_id, L"IBA\\qlgcvnic" );
				child_dev->compatible_id_size = ( wcslen( child_dev->compatible_id ) + 2 )*sizeof( WCHAR );
				swprintf( szDevName, L"%S", device_name );
				wcscpy( child_dev->description, szDevName );
				child_dev->description_size = ( wcslen( child_dev->description ) + 1 )*sizeof( WCHAR );
				child_dev->uniqueinstanceid = instance_id;

				dwRet = send_device_ioctl( UAL_IOC_DEVICE_CREATE, child_dev, sizeof( *child_dev ), NULL, NULL );

				if ( !dwRet ) 
				{
					printf( "Child device creation for CA = %I64X and IOC = %I64X was successful\n", 
						cl_ntoh64( ca_guid ), cl_ntoh64( ioc_guid ) );
					dwRet = write_deviceinfo_to_registry( ca_guid, ioc_guid, instance_id, device_name );
				}
			}
		} 
		else 
		{
			dwRet = -1;
			printf( "No path CA=%I64X to IOC=%I64X found\n",
				cl_ntoh64( ca_guid ), cl_ntoh64( ioc_guid ) );
		}
	}

	if ( dwRet ) 
	{
		printf( "Child device creation for CA = %I64X and IOC = %I64X failed\n", 
			cl_ntoh64( ca_guid ), cl_ntoh64( ioc_guid ) );
	}


	if ( pList ) 
	{
		cl_free( pList );
	}

	return dwRet;
}

DWORD	list_ca_ioc_paths()
{
	DWORD		dwBytesRet = 0, dwRet, n_ca_ioc_path = 0;
	ca_ioc_info_t	*pList = NULL;
	PVOID		pBuf = NULL;

	dwRet = send_device_ioctl( UAL_IOC_LIST, NULL, 0, &pList, &dwBytesRet );

	if ( dwRet ) 
	{
		printf( "Obtaining IOC list from ibiou failed\n" );
	} 
	else if ( dwBytesRet )
       	{
		pBuf = pList;
		n_ca_ioc_path = dwBytesRet/sizeof( ca_ioc_info_t );

		while ( n_ca_ioc_path ) 
		{
			printf( "Channel Adapter %I64X reaches to IOC %I64X\n", 
				cl_ntoh64( pList->ca_guid ), cl_ntoh64( pList->info.profile.ioc_guid ) );
			pList++; n_ca_ioc_path--;
		}
	}
	else 
	{
		printf( "No IOCs are reachable from the host\nPlease check the connections of host\n" );
	}

	if ( pBuf ) 
	{
		cl_free( pBuf );
	}

	return dwRet;
}

int _cdecl main ( int argc, char *argv[] )
{
	char 		device_name[200];
	int		i = 1;
	BOOLEAN		b_list_ioc_paths = FALSE;
	BOOLEAN		b_create_device = FALSE;
	uint64_t	ca_guid, ioc_guid;
	uint32_t	unique_id;
	DWORD		ret = 0;

	while ( i < argc  ) 
	{
		if ( !strcmp( argv[i], "-l" ) ) 
		{
			b_list_ioc_paths = TRUE;
		} 
		else if ( !strcmp( argv[i], "-c" ) ) 
		{
			b_create_device = TRUE;

			if ( argv[++i] ) 
			{
				ca_guid = _strtoui64( argv[i], NULL, 16 );
			} 
			else 
			{
				return usage();
			}
			if ( argv[++i] ) 
			{
				ioc_guid = _strtoui64( argv[i],  NULL, 16 );				
			} 
			else 
			{
				return usage();
			}
			if ( argv[++i] ) 
			{
				unique_id = strtoul( argv[i], NULL, 10 );
			}
			else 
			{
				return usage();
			}
			if ( argv[++i] ) 
			{
				strcpy( device_name, argv[i] );
			} 
			else 
			{
				return usage();
			}

		} 
		else 
		{
			return usage();
		}
		i++;
	}

	if ( b_list_ioc_paths && b_create_device ) 
	{
		return usage();
	}

	else if ( b_create_device ) 
	{
		ret = create_child_device( cl_hton64( ca_guid ), cl_hton64( ioc_guid ), unique_id, device_name );
	} 
	else 
	{
		ret = list_ca_ioc_paths();
	}

	return 0;
}
