/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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
 * Provides the driver entry points for the InfiniBand Bus Driver.
 */

#include <precomp.h>


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "bus_driver.tmh"
#endif

#ifdef WINIB
#define DEFAULT_NODE_DESC	"Mellanox Windows® Host"
#else
#define DEFAULT_NODE_DESC	"OpenIB Windows® Host"
#endif



char	node_desc[IB_NODE_DESCRIPTION_SIZE];
PDEVICE_OBJECT	g_ControlDeviceObject=NULL;
UNICODE_STRING	g_CDO_dev_name, g_CDO_dos_name;

bus_globals_t	bus_globals = {
	BUS_DBG_ERROR,
	TRUE,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	0
};

static void
__read_machine_name( void );

static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_Param_Path );

__drv_dispatchType(IRP_MJ_CREATE)
static DRIVER_DISPATCH bus_drv_open;

__drv_dispatchType(IRP_MJ_CLEANUP)
static DRIVER_DISPATCH bus_drv_cleanup;

__drv_dispatchType(IRP_MJ_CLOSE)
static DRIVER_DISPATCH bus_drv_close;

__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
static DRIVER_DISPATCH bus_drv_ioctl;

/***f* InfiniBand Bus Driver/bus_drv_sysctl
* NAME
*	bus_drv_sysctl
*
* DESCRIPTION
*	Entry point for handling WMI IRPs.
*
* SYNOPSIS
*/
__drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
static DRIVER_DISPATCH bus_drv_sysctl;

/**********/
static DRIVER_UNLOAD bus_drv_unload;

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				UNICODE_STRING				*p_registry_path );


child_device_info_t g_default_device_info;
child_device_info_t g_eoib_default_device_info;


static void __create_default_dev_info(child_device_info_t *pNewDevList)
{
	UNICODE_STRING				keyValue;

	/* DeviceId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\IPoIBP\0");		
	pNewDevList->device_id_size = keyValue.Length + sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->device_id, 
		sizeof(pNewDevList->device_id)/sizeof(wchar_t), keyValue.Buffer );
	/* HardwareId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\IPoIBP\0\0");	
	pNewDevList->hardware_id_size = keyValue.Length + 2*sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->hardware_id, 
		sizeof(pNewDevList->hardware_id)/sizeof(wchar_t), keyValue.Buffer );
	/* CompatibleId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\SID_1000066a00020000\0\0");	
	pNewDevList->compatible_id_size = keyValue.Length + 2*sizeof(WCHAR); //2 
	RtlStringCchCopyW( pNewDevList->compatible_id, 
		sizeof(pNewDevList->compatible_id)/sizeof(wchar_t), keyValue.Buffer );
	/* Device Description */
	RtlInitUnicodeString(&keyValue, L"Mellanox IPoIB Adapter");
	pNewDevList->description_size = keyValue.Length + sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->description, 
		sizeof(pNewDevList->description)/sizeof(wchar_t), keyValue.Buffer );
	/* Pkey */
	RtlInitUnicodeString(&keyValue, L"FFFF");		/* Pkey */
	RtlStringCchCopyW( pNewDevList->pkey, 
		sizeof(pNewDevList->pkey)/sizeof(wchar_t), keyValue.Buffer );    
	pNewDevList->is_eoib = FALSE;
}


static void __create_eoib_default_dev_info(child_device_info_t *pNewDevList)
{
	UNICODE_STRING				keyValue;

	/* DeviceId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\EoIB\0");		
	pNewDevList->device_id_size = keyValue.Length + sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->device_id, 
		sizeof(pNewDevList->device_id)/sizeof(wchar_t), keyValue.Buffer );
	/* HardwareId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\EoIB\0\0");	
	pNewDevList->hardware_id_size = keyValue.Length + 2*sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->hardware_id, 
		sizeof(pNewDevList->hardware_id)/sizeof(wchar_t), keyValue.Buffer );
	/* CompatibleId*/
	RtlInitUnicodeString(&keyValue, L"IBA\\SID_1000066a00020000\0\0");	
	pNewDevList->compatible_id_size = keyValue.Length + 2*sizeof(WCHAR); //2 
	RtlStringCchCopyW( pNewDevList->compatible_id, 
		sizeof(pNewDevList->compatible_id)/sizeof(wchar_t), keyValue.Buffer );
	/* Device Description */
	RtlInitUnicodeString(&keyValue, L"Mellanox EoIB Adapter");
	pNewDevList->description_size = keyValue.Length + sizeof(WCHAR);
	RtlStringCchCopyW( pNewDevList->description, 
		sizeof(pNewDevList->description)/sizeof(wchar_t), keyValue.Buffer );
	/* Pkey */
	RtlInitUnicodeString(&keyValue, L"FFFF");		/* Pkey */
	RtlStringCchCopyW( pNewDevList->pkey, 
		sizeof(pNewDevList->pkey)/sizeof(wchar_t), keyValue.Buffer );
	pNewDevList->is_eoib = TRUE;
}





static void
__read_machine_name( void )
{
	NTSTATUS					status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[2];
	UNICODE_STRING				hostNamePath;
	UNICODE_STRING				hostNameW;
	ANSI_STRING					hostName;

	BUS_ENTER( BUS_DBG_DRV );

	/* Get the host name. */
	RtlInitUnicodeString( &hostNamePath, L"ComputerName\\ComputerName" );
	RtlInitUnicodeString( &hostNameW, NULL );

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );
	cl_memclr( node_desc, sizeof(node_desc) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
	table[0].Name = L"ComputerName";
	table[0].EntryContext = &hostNameW;
	table[0].DefaultType = REG_SZ;
	table[0].DefaultData = &hostNameW;
	table[0].DefaultLength = 0;

	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_CONTROL, 
		hostNamePath.Buffer, table, NULL, NULL );
	if( NT_SUCCESS( status ) )
	{
		/* Convert the UNICODE host name to UTF-8 (ASCII). */
		hostName.Length = 0;
		hostName.MaximumLength = sizeof(node_desc);
		hostName.Buffer = node_desc;
		status = RtlUnicodeStringToAnsiString( &hostName, &hostNameW, FALSE );
		RtlFreeUnicodeString( &hostNameW );
	}
	else
	{
		BUS_TRACE(BUS_DBG_ERROR , ("Failed to get host name.\n") );
		/* Use the default name... */
		RtlStringCbCopyNA( node_desc, sizeof(node_desc),
			DEFAULT_NODE_DESC, sizeof(DEFAULT_NODE_DESC) );
	}
	BUS_EXIT( BUS_DBG_DRV );
}

/************************************************************************************
* name	:	__prepare_pKey_array
*           parses registry string and exrtacts pkey value(s) from it.
*			pkey pattern is 0xABCD
* input	:	UNICODE_STRING *str
* output:	pkey_array
* return:	uint16_t number of pkey(s) found
*************************************************************************************/
static void __prepare_pKey_array(IN const char *str, size_t str_len,OUT pkey_array_t *cur_pkey)
{
	NTSTATUS status;
	size_t i;
	uint8_t j;
	char pkey_str[7+MAX_USER_NAME_SIZE+1];
	ULONG	tmp_val;
	BUS_ENTER( BUS_DBG_DRV );

	CL_ASSERT(cur_pkey);
	CL_ASSERT(str);

	cur_pkey->pkey_num = 0;
	j = 0;
	BUS_TRACE(BUS_DBG_PNP , ("Pkey string '%s', size %d\n", str, (int)str_len) );

	for (i = 0; (i < str_len) && (cur_pkey->pkey_num < MAX_NUM_PKEY) ; i++)
	{
		if(str[i] == ' ')
			continue;

		if( (str[i] != ',') && (str[i] != '\0'))
		{
		    if(j >= sizeof(pkey_str))
				goto bad_pkey1;
			pkey_str[j] = str[i];
			j++;
		}
		else
		{
			// we presume here the following format of pkey_str: 0x<4-digit-pkey>-<up-to-16-symbol-name>
			if (j < 8)
				goto bad_pkey2;
			pkey_str[j] = '\0';
			pkey_str[6] = '\0';

			//	name sanity checks
			if ( strlen(&pkey_str[7]) == 0 )
				goto bad_name1;
			if ( strlen(&pkey_str[7]) >= MAX_USER_NAME_SIZE )
				goto bad_name2;
			if ( strstr(&pkey_str[7],"-") )
				goto bad_name3;
			if ( strstr(&pkey_str[7],":") )
				goto bad_name3;
			if ( strstr(&pkey_str[7],";") )
				goto bad_name3;
			if ( strstr(&pkey_str[7],",") )
				goto bad_name3;
			if ( !(pkey_str[7] == 'i' || pkey_str[7] == 'I' || pkey_str[7] == 'e' || pkey_str[7] == 'E') )
				goto bad_name4;
			if ( pkey_str[7] == 'e' || pkey_str[7] == 'E' )
				goto bad_name5;


			status = RtlCharToInteger(&pkey_str[2],16,&tmp_val);
			if(! NT_SUCCESS(status))
				goto bad_pkey3;
			cur_pkey->pkey_array[cur_pkey->pkey_num] = (uint16_t)tmp_val;
			status = RtlStringCchCopyA(cur_pkey->name[cur_pkey->pkey_num], sizeof(cur_pkey->name[cur_pkey->pkey_num]), &pkey_str[7]);
			if(! NT_SUCCESS(status))
				goto bad_name6;
			cur_pkey->pkey_num++;
			j = 0;
		}
	}
	goto exit;

bad_pkey1:
	BUS_TRACE(BUS_DBG_ERROR ,
		("Incorrect format of pkey value, j %d\n", j) );
	goto err;

bad_pkey2:
	BUS_TRACE(BUS_DBG_ERROR ,
		("Incorrect format of pkey value: name is too short, j %d\n", j) );
	goto err;

bad_pkey3:
	BUS_TRACE(BUS_DBG_ERROR ,
		("Failed to convert pkey, status = 0x%08X\n",status) );
	goto err;

bad_name1:	
	BUS_TRACE(BUS_DBG_ERROR ,
		("ERROR: The name is absent.\n", &pkey_str[7], MAX_USER_NAME_SIZE-1 ));
	goto err;

bad_name2:	
	BUS_TRACE(BUS_DBG_ERROR ,
		("ERROR: The name '%s' is too long (%d). Max size is %d.\n", &pkey_str[7], strlen(&pkey_str[7]), MAX_USER_NAME_SIZE-1 ));
	goto err;

bad_name3:	
	BUS_TRACE(BUS_DBG_ERROR ,
		("ERROR: Illegal name '%s'. The name shouldn't contain the following characters: '-', ';', ':', ','\n", &pkey_str[7]));
	goto err;

bad_name4:	
	BUS_TRACE(BUS_DBG_ERROR ,
		("ERROR: Illegal name '%s'. The name should start from 'i' for IPoIB or from for EoIB\n", &pkey_str[7]));
	goto err;

bad_name5:	
	BUS_TRACE(BUS_DBG_ERROR ,
		("ERROR: Illegal name '%s'. EoIB is not supported now\n", &pkey_str[7]));
	goto err;

bad_name6:
	BUS_TRACE(BUS_DBG_ERROR ,
		("Failed to copy name, status = 0x%08X\n",status) );
	goto err;

err:	
	cur_pkey->pkey_num = 0;
exit:
	BUS_EXIT( BUS_DBG_DRV );
}


static void	_free_static_iodevices()
{
	child_device_info_list_t *pDevList, *pDevList1;

	pDevList = bus_globals.p_device_list;

	while(pDevList)
	{
		pDevList1 = pDevList->next_device_info;
		cl_free(pDevList);
		pDevList = pDevList1;
	}

}

static NTSTATUS __create_static_devices(PUNICODE_STRING p_param_path)
{
	RTL_QUERY_REGISTRY_TABLE	table[2];
	UNICODE_STRING				keyPath;
	UNICODE_STRING				keyValue;
	UNICODE_STRING				defaultKeyValue;
	UNICODE_STRING				child_name;
	WCHAR						*key_path_buffer;
	WCHAR						*key_value_buffer;
	WCHAR						*static_child_name;
	NTSTATUS					status;
	#define BUF_SIZE			256		/* use hard-coded size to make it simple*/

	cl_memclr( table, sizeof(table) );

	
	key_path_buffer = cl_zalloc(BUF_SIZE*sizeof(WCHAR)*3);

	if(!key_path_buffer)
	{
		BUS_TRACE(BUS_DBG_ERROR ,("Not enough memory for key_path_buffer.\n") );
		status = STATUS_UNSUCCESSFUL;
		goto __create_static_devices_exit;
	}

	key_value_buffer = key_path_buffer + BUF_SIZE;
	static_child_name = key_value_buffer + BUF_SIZE;

	RtlInitUnicodeString( &keyPath, NULL );
	keyPath.MaximumLength = BUF_SIZE*sizeof(WCHAR);
	keyPath.Buffer = key_path_buffer;

	RtlInitUnicodeString( &keyValue, NULL );
	keyValue.MaximumLength = BUF_SIZE*sizeof(WCHAR);
	keyValue.Buffer = key_value_buffer;

	RtlInitUnicodeString( &child_name, NULL );
	child_name.MaximumLength = BUF_SIZE*sizeof(WCHAR);
	child_name.Buffer = static_child_name;


	RtlCopyUnicodeString( &keyPath, p_param_path );

	RtlInitUnicodeString(&defaultKeyValue, L"IPoIB\0\0");

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND;
	table[0].Name = L"StaticChild";
	table[0].EntryContext = &child_name;
	table[0].DefaultType = REG_MULTI_SZ;
	table[0].DefaultData = defaultKeyValue.Buffer;
	table[0].DefaultLength = defaultKeyValue.Length;

	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		keyPath.Buffer, table, NULL, NULL );

	if(NT_SUCCESS(status))
	{
		WCHAR *curChild;
		child_device_info_list_t *pPrevList, *pNewDevList;

		curChild = static_child_name;
		pPrevList = bus_globals.p_device_list;
		while(*curChild)
		{
			RtlCopyUnicodeString(&keyPath, p_param_path);
			RtlAppendUnicodeToString(&keyPath, L"\\");
			RtlAppendUnicodeToString(&keyPath, curChild);

			pNewDevList = cl_zalloc(sizeof(child_device_info_list_t));
			if(!pNewDevList)
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Not enough memory for key_path_buffer.\n") );
				status = STATUS_UNSUCCESSFUL;
				goto __create_static_devices_exit;
			}
			pNewDevList->next_device_info = NULL;

			if(pPrevList == NULL)
			{
				bus_globals.p_device_list = pNewDevList;
			}else
			{
				pPrevList->next_device_info = pNewDevList;
			}

			pPrevList = pNewDevList;

			/* get DeviceId*/
			RtlInitUnicodeString(&defaultKeyValue, L"IBA\\IPoIB\0");
			RtlInitUnicodeString( &keyValue, NULL );
			keyValue.MaximumLength = sizeof(pNewDevList->io_device_info.device_id);
			keyValue.Buffer = pNewDevList->io_device_info.device_id;

			/* Setup the table entries. */
			table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
			table[0].Name = L"DeviceId";
			table[0].EntryContext = &keyValue; 
			table[0].DefaultType = REG_SZ;
			table[0].DefaultData = defaultKeyValue.Buffer;
			table[0].DefaultLength = defaultKeyValue.Length;

			status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
				keyPath.Buffer, table, NULL, NULL );
			if(!NT_SUCCESS(status))
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Failed to read DeviceId.\n") );
				goto __create_static_devices_exit;
			}
			pNewDevList->io_device_info.device_id_size = keyValue.Length + sizeof(WCHAR);

			/* Get HardwareId*/
			RtlInitUnicodeString(&defaultKeyValue, L"IBA\\IPoIB\0\0");
			RtlInitUnicodeString( &keyValue, NULL );
			keyValue.MaximumLength = sizeof(pNewDevList->io_device_info.hardware_id);
			keyValue.Buffer = pNewDevList->io_device_info.hardware_id;

			/* Setup the table entries. */
			table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
			table[0].Name = L"HardwareId";
			table[0].EntryContext = &keyValue; 
			table[0].DefaultType = REG_MULTI_SZ;
			table[0].DefaultData = defaultKeyValue.Buffer;
			table[0].DefaultLength = defaultKeyValue.Length;

			status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
				keyPath.Buffer, table, NULL, NULL );
			if(!NT_SUCCESS(status))
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Failed to read HardwareId.\n") );
				goto __create_static_devices_exit;
			}
			pNewDevList->io_device_info.hardware_id_size = keyValue.Length + 2*sizeof(WCHAR);

			/* Get CompatibleId*/
			RtlInitUnicodeString(&defaultKeyValue, L"IBA\\SID_1000066a00020000\0\0");
			RtlInitUnicodeString( &keyValue, NULL );
			keyValue.MaximumLength = sizeof(pNewDevList->io_device_info.compatible_id);
			keyValue.Buffer = pNewDevList->io_device_info.compatible_id;

			/* Setup the table entries. */
			table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
			table[0].Name = L"CompatibleId";
			table[0].EntryContext = &keyValue; 
			table[0].DefaultType = REG_MULTI_SZ;
			table[0].DefaultData = defaultKeyValue.Buffer;
			table[0].DefaultLength = defaultKeyValue.Length;

			status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
				keyPath.Buffer, table, NULL, NULL );
			if(!NT_SUCCESS(status))
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Failed to read CompatibleId.\n") );
				goto __create_static_devices_exit;
			}
			pNewDevList->io_device_info.compatible_id_size = keyValue.Length + 2*sizeof(WCHAR); //2 null

			/* Get Description*/
			RtlInitUnicodeString(&defaultKeyValue, L"OpenIB IPoIB Adapter");
			RtlInitUnicodeString( &keyValue, NULL );
			keyValue.MaximumLength = sizeof(pNewDevList->io_device_info.description);
			keyValue.Buffer = pNewDevList->io_device_info.description;

			/* Setup the table entries. */
			table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
			table[0].Name = L"Description";
			table[0].EntryContext = &keyValue; 
			table[0].DefaultType = REG_SZ;
			table[0].DefaultData = defaultKeyValue.Buffer;
			table[0].DefaultLength = defaultKeyValue.Length;

			status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
				keyPath.Buffer, table, NULL, NULL );
			if(!NT_SUCCESS(status))
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Failed to read Description.\n") );
				goto __create_static_devices_exit;
			}

			pNewDevList->io_device_info.description_size = keyValue.Length + sizeof(WCHAR);


			if((pNewDevList->io_device_info.description_size > MAX_DEVICE_ID_LEN) ||
			   (pNewDevList->io_device_info.hardware_id_size > MAX_DEVICE_ID_LEN) ||
			   (pNewDevList->io_device_info.compatible_id_size > MAX_DEVICE_ID_LEN) ||
			   (pNewDevList->io_device_info.device_id_size > MAX_DEVICE_ID_LEN)
			   )
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Id or description size is too big.\n") );
				status = STATUS_UNSUCCESSFUL;
				goto __create_static_devices_exit;
			}

			/* Get Pkey */
			RtlInitUnicodeString(&defaultKeyValue, L"FFFF");
			RtlInitUnicodeString( &keyValue, NULL );
			keyValue.MaximumLength = sizeof(pNewDevList->io_device_info.pkey);
			keyValue.Buffer = pNewDevList->io_device_info.pkey;

			/* Setup the table entries. */
			table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
			table[0].Name = L"PartitionKey";
			table[0].EntryContext = &keyValue; 
			table[0].DefaultType = REG_SZ;
			table[0].DefaultData = defaultKeyValue.Buffer;
			table[0].DefaultLength = defaultKeyValue.Length;

			status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
				keyPath.Buffer, table, NULL, NULL );
			if(!NT_SUCCESS(status))
			{
				BUS_TRACE(BUS_DBG_ERROR ,("Failed to read PartitionKey.\n") );
				goto __create_static_devices_exit;
			}

			while(*curChild) curChild++;
			curChild++;
		}
	}

__create_static_devices_exit:
	if(key_path_buffer)
	{
		cl_free(key_path_buffer);
	}

	if(!NT_SUCCESS(status))
	{
		_free_static_iodevices();
	}

	return status;
}

static pkey_conf_t*
create_pkey_conf(__in pkey_conf_t **pp_cur_conf,__in char *guid_str,__in uint32_t guid_str_len)
{
	NTSTATUS status;
	char	tmp_char;
	uint32_t tmp_val;

	if (! *pp_cur_conf)
		pp_cur_conf = &bus_globals.p_pkey_conf;
	else
		pp_cur_conf = &((*pp_cur_conf)->next_conf);

	*pp_cur_conf = cl_zalloc( sizeof( pkey_conf_t ) );
	if (!(*pp_cur_conf) )
	{
		BUS_TRACE(BUS_DBG_ERROR ,
		("Failed to allocate pkey configuration\n") );
		return NULL;   	
	}

	tmp_char = guid_str[1 + guid_str_len/2];
	guid_str[1 + guid_str_len/2] = '\0';
	status = RtlCharToInteger(&guid_str[2],16,(PULONG)&tmp_val);
	if(! NT_SUCCESS(status))
	{
		cl_free((*pp_cur_conf));
		(*pp_cur_conf) = NULL;
		BUS_TRACE(BUS_DBG_ERROR ,
		("Failed to convert, status = 0x%08X\n",status) );
		return NULL;   	
	}
	guid_str[1 + guid_str_len/2] = tmp_char;
	(*pp_cur_conf)->pkeys_per_port.port_guid = tmp_val;

	status = RtlCharToInteger(&guid_str[1 + guid_str_len/2],16,(PULONG)&tmp_val);
	if(! NT_SUCCESS(status))
	{
		cl_free((*pp_cur_conf));
		(*pp_cur_conf) = NULL;
		BUS_TRACE(BUS_DBG_ERROR ,
		("Failed to convert, status = 0x%08X\n",status) );
		return NULL;   	
	}
	(*pp_cur_conf)->pkeys_per_port.port_guid = ((*pp_cur_conf)->pkeys_per_port.port_guid << 32) | tmp_val;
	return (*pp_cur_conf);
}

/************************************************************************
* name:		__build_pkeys_per_port
*			extracts pkeys and port guids from registry string.
*			builds pkey array per port
* input:	UNICODE_STRING *str
* return:	NTSTATUS
************************************************************************/
static NTSTATUS
__build_pkeys_per_port(IN const UNICODE_STRING *str)
{
	NTSTATUS    status;
	ANSI_STRING ansi_str;
	uint32_t i,j;
	char *p_end, *p_start;
	boolean_t	port_guid_found;
	pkey_conf_t	*cur_pkey_conf = NULL;
	char tmp_guid[32] = {'\0'};
	p_start = NULL;

	status = RtlUnicodeStringToAnsiString(&ansi_str,str,TRUE);
	if(! NT_SUCCESS(status))
	{
		BUS_TRACE(BUS_DBG_ERROR ,
		("RtlUnicodeStringToAnsiString returned 0x%.8x\n", status) );
		return status;
	}

	port_guid_found = FALSE;
	j = 0;
	for ( i = 0; i < ansi_str.MaximumLength; i++)
	{
		if(! port_guid_found)
		{
			if(ansi_str.Buffer[i] == ':')
			{
				port_guid_found = TRUE;
				tmp_guid[j] = '\0';
				cur_pkey_conf = create_pkey_conf(&cur_pkey_conf,(char*)tmp_guid,j);
				if(! cur_pkey_conf)
				{
				   RtlFreeAnsiString(&ansi_str);
				   BUS_TRACE(BUS_DBG_ERROR ,
				   ("Failed to create pkey configuration\n"));
				   return STATUS_INVALID_PARAMETER;
				}
			    RtlZeroMemory(tmp_guid,sizeof(tmp_guid));
				j = 0;
				p_start = NULL;
			}
			else
			{
				tmp_guid[j] = ansi_str.Buffer[i]; 
				j++;
				continue;
			}
		}
		else
		{
			if(!p_start)
				p_start = &ansi_str.Buffer[i]; 

			if(ansi_str.Buffer[i] == ';')
			{
				p_end = &ansi_str.Buffer[i];
				ansi_str.Buffer[i] = '\0';
				__prepare_pKey_array(p_start,(size_t)(p_end - p_start) + 1,&cur_pkey_conf->pkeys_per_port);
				if (!cur_pkey_conf->pkeys_per_port.pkey_num)
				{
					RtlFreeAnsiString(&ansi_str);
					BUS_TRACE(BUS_DBG_ERROR ,
					("Failed to create pkey configuration\n"));
					return STATUS_INVALID_PARAMETER;
				}

				ansi_str.Buffer[i] = ';';
				p_start = NULL;
				port_guid_found = FALSE;
			}
		}
	}
    RtlFreeAnsiString(&ansi_str);
	return STATUS_SUCCESS;
}

static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_registry_path )
{
	NTSTATUS					status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[12];
	UNICODE_STRING				param_path;
	UNICODE_STRING				pkeyString;
	UNICODE_STRING				empty_string;

	BUS_ENTER( BUS_DBG_DRV );

	__read_machine_name();

	RtlInitUnicodeString( &empty_string,NULL);
	RtlInitUnicodeString( &param_path, NULL );
	param_path.MaximumLength = p_registry_path->Length + 
		sizeof(L"\\Parameters");
	param_path.Buffer = cl_zalloc( param_path.MaximumLength );
	if( !param_path.Buffer )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Failed to allocate parameters path buffer.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlAppendUnicodeStringToString( &param_path, p_registry_path );
	RtlAppendUnicodeToString( &param_path, L"\\Parameters" );
	RtlInitUnicodeString( &pkeyString, NULL );
    pkeyString.MaximumLength = 1024*sizeof(WCHAR);
    pkeyString.Buffer = cl_zalloc( pkeyString.MaximumLength );
    if( !pkeyString.Buffer )
	{
		cl_free(param_path.Buffer);
		BUS_TRACE_EXIT( BUS_DBG_ERROR,
                      ("Failed to allocate parameters path pkeyString.\n") );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[0].Name = L"ReportPortNIC";
	table[0].EntryContext = &bus_globals.b_report_port_nic;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &bus_globals.b_report_port_nic;
	table[0].DefaultLength = sizeof(ULONG);

	table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[1].Name = L"DebugFlags";
	table[1].EntryContext = &bus_globals.dbg_lvl;
	table[1].DefaultType = REG_DWORD;
	table[1].DefaultData = &bus_globals.dbg_lvl;
	table[1].DefaultLength = sizeof(ULONG);

	table[2].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[2].Name = L"IbalDebugLevel";
	table[2].EntryContext = &g_al_dbg_level;
	table[2].DefaultType = REG_DWORD;
	table[2].DefaultData = &g_al_dbg_level;
	table[2].DefaultLength = sizeof(ULONG);

	table[3].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[3].Name = L"IbalDebugFlags";
	table[3].EntryContext = &g_al_dbg_flags;
	table[3].DefaultType = REG_DWORD;
	table[3].DefaultData = &g_al_dbg_flags;
	table[3].DefaultLength = sizeof(ULONG);
	

	table[4].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[4].Name = L"SmiPollInterval";
	table[4].EntryContext = &g_smi_poll_interval;
	table[4].DefaultType = REG_DWORD;
	table[4].DefaultData = &g_smi_poll_interval;
	table[4].DefaultLength = sizeof(ULONG);

	table[5].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[5].Name = L"IocQueryTimeout";
	table[5].EntryContext = &g_ioc_query_timeout;
	table[5].DefaultType = REG_DWORD;
	table[5].DefaultData = &g_ioc_query_timeout;
	table[5].DefaultLength = sizeof(ULONG);

	table[6].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[6].Name = L"IocQueryRetries";
	table[6].EntryContext = &g_ioc_query_retries;
	table[6].DefaultType = REG_DWORD;
	table[6].DefaultData = &g_ioc_query_retries;
	table[6].DefaultLength = sizeof(ULONG);

	table[7].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[7].Name = L"IocPollInterval";
	table[7].EntryContext = &g_ioc_poll_interval;
	table[7].DefaultType = REG_DWORD;
	table[7].DefaultData = &g_ioc_poll_interval;
	table[7].DefaultLength = sizeof(ULONG);

	table[8].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[8].Name = L"PartitionKey";
	table[8].EntryContext = &pkeyString;
	table[8].DefaultType  = REG_SZ;
	table[8].DefaultData  = &empty_string;
	table[8].DefaultLength = 0;

	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		param_path.Buffer, table, NULL, NULL );
	if (NT_SUCCESS(status))
	{
			if( !NT_SUCCESS(__build_pkeys_per_port(&pkeyString)))
				BUS_TRACE(BUS_DBG_ERROR ,
						 ("Failed to build pkey configuration\n"));

			if(!NT_SUCCESS(__create_static_devices(&param_path))){
				BUS_TRACE(BUS_DBG_ERROR ,
						 ("Failed to create devices\n"));
			}
	}
#if DBG & !defined( EVENT_TRACING )
	if( g_al_dbg_flags & AL_DBG_ERR )
		g_al_dbg_flags |= CL_DBG_ERROR;
#endif

	BUS_TRACE(BUS_DBG_DRV,
			("debug level %d debug flags 0x%.8x\n",
			g_al_dbg_level,
			g_al_dbg_flags));

	// debug
	bus_globals.dbg_lvl = BUS_DBG_ERROR | BUS_DBG_POWER;

	cl_free( pkeyString.Buffer );
	cl_free( param_path.Buffer );
	BUS_EXIT( BUS_DBG_DRV );
	return status;
}


static NTSTATUS
bus_drv_open(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	UNUSED_PARAM( p_dev_obj );

	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	if ( !bus_globals.started )
	{
		BUS_TRACE(BUS_DBG_DRV ,
			("bus_drv_open: IBBUS has not yet started!\n") );
		p_irp->IoStatus.Status = STATUS_INVALID_DEVICE_STATE;
	}
	else
		p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	return STATUS_SUCCESS;
}


static NTSTATUS
bus_drv_cleanup(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS			status;

	UNUSED_PARAM( p_dev_obj );

	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	/*
	 * Note that we don't acquire the remove and stop lock on close to allow
	 * applications to close the device when the locks are already held.
	 */
	status = cl_to_ntstatus( al_dev_close( p_irp ) );

	/* Complete the IRP. */
	p_irp->IoStatus.Status = status;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	return status;
}


static NTSTATUS
bus_drv_close(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS			status;

	UNUSED_PARAM( p_dev_obj );

	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	/*
	 * Note that we don't acquire the remove and stop lock on close to allow
	 * applications to close the device when the locks are already held.
	 */
	status = cl_to_ntstatus( al_dev_close( p_irp ) );

	/* Complete the IRP. */
	p_irp->IoStatus.Status = status;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	return STATUS_SUCCESS;
}


static NTSTATUS
bus_drv_ioctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS			status;
	bus_fdo_ext_t		*p_ext;
	PIO_STACK_LOCATION	p_io_stack;

	/* Get the extension. */
	p_ext = p_dev_obj->DeviceExtension;

	/* Get the stack location. */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	/* Acquire the stop lock. */
	status = IoAcquireRemoveLock( &p_ext->cl_ext.stop_lock, p_irp );
	if( !NT_SUCCESS( status ) )
	{
		p_irp->IoStatus.Status = status;
		p_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		BUS_EXIT( BUS_DBG_DRV );
		return status;
	}

	/* Acquire the remove lock. */
	status = IoAcquireRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
	if( !NT_SUCCESS( status ) )
	{
		IoReleaseRemoveLock( &p_ext->cl_ext.stop_lock, p_irp );
		p_irp->IoStatus.Status = status;
		p_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
		BUS_EXIT( BUS_DBG_DRV );
		return status;
	}
	
	status = cl_to_ntstatus( al_dev_ioctl( p_irp ) );
	
	/* Only pass down if not handled and not PDO device. */
	if( status == STATUS_INVALID_DEVICE_REQUEST )
    {
        if( p_ext->cl_ext.p_next_do != NULL )
	{
		IoSkipCurrentIrpStackLocation( p_irp );
		status = IoCallDriver( p_ext->cl_ext.p_next_do, p_irp );
	}
        else
        {
            p_irp->IoStatus.Status = status;
            p_irp->IoStatus.Information = 0;
            IoCompleteRequest( p_irp, IO_NO_INCREMENT );
        }
    }

	/* Release the remove and stop locks. */
	IoReleaseRemoveLock( &p_ext->cl_ext.remove_lock, p_irp );
	IoReleaseRemoveLock( &p_ext->cl_ext.stop_lock, p_irp );

	return status;
}

cl_status_t
bus_add_pkey(cl_ioctl_handle_t			h_ioctl)
{
	cl_status_t 				status;
	pkey_array_t 				*pkeys;
	PIO_STACK_LOCATION			pIoStack;
	int cnt;

	BUS_ENTER( BUS_DBG_DRV );

	pIoStack = IoGetCurrentIrpStackLocation(h_ioctl);
	if ( (! h_ioctl->AssociatedIrp.SystemBuffer) || 
		 pIoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof (pkey_array_t))
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Invalid parameters.\n") );
		return CL_INVALID_PARAMETER;
	}

	pkeys =  (pkey_array_t*)h_ioctl->AssociatedIrp.SystemBuffer;

	// For user mode devices we only allow to create ipoib devices.
	for (cnt = 0; cnt < pkeys->pkey_num; cnt++) 
	{
		pkeys->port_type[cnt] = PORT_TYPE_IPOIB;
		pkeys->UniqueId[cnt] = (uint64_t)(-1);
		pkeys->Location[cnt] = (uint32_t)(-1);
	}

	/* create additional pdo */
	status = port_mgr_pkey_add(pkeys);
	if (status != CL_SUCCESS)
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("port_mgr_pkey_add returned %08x.\n", status) );
	}

	BUS_EXIT( BUS_DBG_DRV );
	return status;
}

cl_status_t
bus_rem_pkey(cl_ioctl_handle_t			h_ioctl)
{
	cl_status_t 				status;
	pkey_array_t 				*pkeys;
	PIO_STACK_LOCATION			pIoStack;

	BUS_ENTER( BUS_DBG_DRV );

	pIoStack = IoGetCurrentIrpStackLocation(h_ioctl);
	if ( (! h_ioctl->AssociatedIrp.SystemBuffer) || 
		 pIoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof (pkey_array_t))
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("Invalid parameters.\n") );
		return CL_INVALID_PARAMETER;
	}

	pkeys =  (pkey_array_t*)h_ioctl->AssociatedIrp.SystemBuffer;

	/* removes pdo */
	status = port_mgr_pkey_rem(pkeys);
	if (! NT_SUCCESS(status))
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("port_mgr_pkey_rem returned %08x.\n", status) );
	}

	BUS_EXIT( BUS_DBG_DRV );
	return status;
}
static NTSTATUS	
bus_drv_sysctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS		status;
	bus_fdo_ext_t	*p_ext;

	BUS_ENTER( BUS_DBG_DRV );

	CL_ASSERT( p_dev_obj );
	CL_ASSERT( p_irp );

	p_ext = p_dev_obj->DeviceExtension;

	if( p_ext->cl_ext.p_next_do )
	{
		IoSkipCurrentIrpStackLocation( p_irp );
		status = IoCallDriver( p_ext->cl_ext.p_next_do, p_irp );
	}
	else
	{
		status = p_irp->IoStatus.Status;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	}

	BUS_EXIT( BUS_DBG_DRV );
	return status;
}


static void
bus_drv_unload(
	IN				DRIVER_OBJECT				*p_driver_obj )
{
	pkey_conf_t *cur_conf,*tmp;
	UNUSED_PARAM( p_driver_obj );

	FipDrvUnload(p_driver_obj);


	BUS_ENTER( BUS_DBG_DRV );

	mcast_mgr_shutdown();
	
	cur_conf = bus_globals.p_pkey_conf;
	while(cur_conf)
	{
		tmp = cur_conf;
		cur_conf = cur_conf->next_conf;
		cl_free(tmp);
	}

	_free_static_iodevices();


	CL_DEINIT;

#if defined(EVENT_TRACING)
	WPP_CLEANUP(p_driver_obj);
#endif

	BUS_TRACE_EXIT( BUS_DBG_DRV, 
		("=====> IBBUS: bus_drv_unload exited\n") );
}


NTSTATUS
DriverEntry(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				UNICODE_STRING				*p_registry_path )
{
	NTSTATUS		status;

	BUS_ENTER( BUS_DBG_DRV );

#if defined(EVENT_TRACING)
	WPP_INIT_TRACING(p_driver_obj ,p_registry_path);
#endif

	status = CL_INIT;
	if( !NT_SUCCESS(status) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("cl_init returned %08X.\n", status) );
		goto exit_wpp;
	}

	/* Store the driver object pointer in the global parameters. */
	bus_globals.p_driver_obj = p_driver_obj;
	bus_st_init();
	g_stat.drv.p_globals = &bus_globals;

	/* Get the registry values. */
	status = __read_registry( p_registry_path );
	if( !NT_SUCCESS(status) )
	{
		BUS_TRACE_EXIT( BUS_DBG_ERROR, ("__read_registry returned %08x.\n", status) );
		goto exit_cl_deinit;
	}

	/* create default device descrition for Partition Manager */
	__create_default_dev_info( &g_default_device_info );   
	__create_eoib_default_dev_info( &g_eoib_default_device_info );    
	
	/* Setup the entry points. */
	p_driver_obj->MajorFunction[IRP_MJ_CREATE] = bus_drv_open;
	p_driver_obj->MajorFunction[IRP_MJ_CLEANUP] = bus_drv_cleanup;
	p_driver_obj->MajorFunction[IRP_MJ_CLOSE] = bus_drv_close;
	p_driver_obj->MajorFunction[IRP_MJ_PNP] = cl_pnp;
	p_driver_obj->MajorFunction[IRP_MJ_POWER] = cl_power;
	p_driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = bus_drv_ioctl;
	p_driver_obj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = bus_drv_sysctl;
	p_driver_obj->DriverUnload = bus_drv_unload;
	p_driver_obj->DriverExtension->AddDevice = bus_add_device;

	// Mutex to synchronize multiple threads creating & deleting 
	// control deviceobjects. 

	KeInitializeEvent(&g_ControlEvent, SynchronizationEvent, TRUE);
	g_bfi_InstanceCount = 0;
	memset( (void*)g_bus_filters, 0, sizeof(g_bus_filters) );


	status = FipDriverEntry(p_driver_obj, p_registry_path);
	if( !NT_SUCCESS(status) )
	{
		CL_DEINIT;
		BUS_TRACE_EXIT( BUS_DBG_ERROR, 
			("FipDriverEntry returned %08x.\n", status) );
		return status;
	}

	mcast_mgr_init();
    
	BUS_TRACE_EXIT( BUS_DBG_DRV, 
		("=====> IBBUS: Driver exited\n") );
	return STATUS_SUCCESS;

exit_cl_deinit:
	CL_DEINIT;

exit_wpp:
#if defined(EVENT_TRACING)
	WPP_CLEANUP(p_driver_obj);
#endif
    return status;
}
