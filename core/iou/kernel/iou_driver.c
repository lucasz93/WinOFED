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
 * Provides the driver entry points for the InfiniBand I/O Unit Bus Driver.
 */

#include <complib/cl_types.h>
#include "iou_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "iou_driver.tmh"
#endif
#include "iou_pnp.h"
#include "al_dev.h"
#include "iou_ioc_mgr.h"
#include <complib/cl_init.h>
#include <complib/cl_bus_ifc.h>

iou_globals_t	iou_globals = {
	NULL
};

uint32_t		g_iou_dbg_level = TRACE_LEVEL_ERROR;
uint32_t		g_iou_dbg_flags = 0x00000fff;

static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_Param_Path );

static NTSTATUS
iou_drv_open(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp );

static NTSTATUS
iou_drv_close(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp );

static NTSTATUS
iou_drv_cleanup(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP					*p_irp);

static NTSTATUS
iou_drv_ioctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp );

/***f* InfiniBand Bus Driver/iou_sysctl
* NAME
*	iou_sysctl
*
* DESCRIPTION
*	Entry point for handling WMI IRPs.
*
* SYNOPSIS
*/
static NTSTATUS
iou_sysctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp );

/**********/

static void
iou_unload(
	IN				DRIVER_OBJECT				*p_driver_obj );

NTSTATUS
DriverEntry(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				UNICODE_STRING				*p_registry_path );


static void	_free_child_device_list()
{
	child_device_info_list_t *pDevList, *pDevList1;

	pDevList = iou_globals.p_device_list;

	while( pDevList )
	{
		pDevList1 = pDevList->next_device_info;
		cl_free( pDevList );
		pDevList = pDevList1;
	}

}

static NTSTATUS _build_child_device_list( PUNICODE_STRING p_param_path )
{
	RTL_QUERY_REGISTRY_TABLE	table[2];
	UNICODE_STRING			keyPath;
	UNICODE_STRING			keyValue;
	UNICODE_STRING			child_name;
	WCHAR				*key_path_buffer;
	WCHAR				*key_value_buffer;
	WCHAR				*static_child_name;
	NTSTATUS			status;
	uint32_t			instanceid = 1;
	HANDLE				hParamKey = NULL;
	UNICODE_STRING			valuename;
	ULONG				sizeofbuf;
	PVOID				pBuf = NULL;
	PKEY_VALUE_FULL_INFORMATION	pKeyVal = NULL;
	uint64_t			guid_val = 0;
	OBJECT_ATTRIBUTES		objattr;
	WCHAR 				*curChild;
	child_device_info_list_t 	*pPrevList, *pNewDevList;

	cl_memclr(  table, sizeof( table) );

	/* use hard-coded size 256 to make it simple*/
	key_path_buffer = cl_zalloc( 256*sizeof( WCHAR )*3 ) ;

	if( !key_path_buffer )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR ,( "Not enough memeory for key_path_buffer.\n" ) );
		status = STATUS_UNSUCCESSFUL;
		goto _build_child_device_list_exit;
	}

	key_value_buffer = key_path_buffer + 256;
	static_child_name = key_value_buffer + 256;

	RtlInitUnicodeString( &keyPath, NULL );
	keyPath.MaximumLength = 256*sizeof( WCHAR );
	keyPath.Buffer = key_path_buffer;

	RtlInitUnicodeString( &keyValue, NULL );
	keyValue.MaximumLength = 256*sizeof( WCHAR );
	keyValue.Buffer = key_value_buffer;

	RtlInitUnicodeString( &child_name, NULL );
	child_name.MaximumLength = 256*sizeof( WCHAR );
	child_name.Buffer = static_child_name;


	RtlCopyUnicodeString( &keyPath, p_param_path );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND;
	table[0].Name = L"StaticChild";
	table[0].EntryContext = &child_name;
	table[0].DefaultType = REG_NONE;

	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
		keyPath.Buffer, table, NULL, NULL );

	if( !NT_SUCCESS( status ) )
	{
		if ( status == STATUS_INVALID_PARAMETER )
		{
			IOU_PRINT(TRACE_LEVEL_WARNING, IOU_DBG_ERROR, 
					("Failed to read registry values\n") );
		}
		goto _build_child_device_list_exit;
	}

	curChild = static_child_name;
	pPrevList = iou_globals.p_device_list;
	while( *curChild )
	{
		RtlCopyUnicodeString( &keyPath, p_param_path );
		RtlAppendUnicodeToString( &keyPath, L"\\" );
		RtlAppendUnicodeToString( &keyPath, curChild );

		pNewDevList = cl_zalloc( sizeof( child_device_info_list_t ) );
		if( !pNewDevList )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Not enough memeory for key_path_buffer.\n" ) );
			status = STATUS_UNSUCCESSFUL;
			goto _build_child_device_list_exit;
		}
		pNewDevList->next_device_info = NULL;

		if( pPrevList == NULL )
		{
			iou_globals.p_device_list = pNewDevList;
		}
		else
		{
			pPrevList->next_device_info = pNewDevList;
		}

		pPrevList = pNewDevList;

		RtlInitUnicodeString( &keyValue, NULL );
		keyValue.MaximumLength = sizeof( pNewDevList->io_device_info.device_id );
		keyValue.Buffer = pNewDevList->io_device_info.device_id;

		/* Setup the table entries. */
		table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
		table[0].Name = L"DeviceId";
		table[0].EntryContext = &keyValue; 
		table[0].DefaultType = REG_NONE;

		status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
			keyPath.Buffer, table, NULL, NULL );
		if( !NT_SUCCESS( status ) )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Failed to read DeviceId.\n" ) );
			goto _build_child_device_list_exit;
		}
		pNewDevList->io_device_info.device_id_size = keyValue.Length + sizeof( WCHAR);

		/* Get HardwareId*/
		RtlInitUnicodeString( &keyValue, NULL );
		keyValue.MaximumLength = sizeof( pNewDevList->io_device_info.hardware_id );
		keyValue.Buffer = pNewDevList->io_device_info.hardware_id;

		/* Setup the table entries. */
		table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
		table[0].Name = L"HardwareId";
		table[0].EntryContext = &keyValue; 
		table[0].DefaultType = REG_NONE;

		status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
			keyPath.Buffer, table, NULL, NULL );
		if( !NT_SUCCESS( status ) )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR ,
					( "Failed to read HardwareId.\n" ) );
			goto _build_child_device_list_exit;
		}
		pNewDevList->io_device_info.hardware_id_size = keyValue.Length + 2*sizeof( WCHAR );
		/* Get CompatibleId*/
		RtlInitUnicodeString( &keyValue, NULL );
		keyValue.MaximumLength = sizeof( pNewDevList->io_device_info.compatible_id );
		keyValue.Buffer = pNewDevList->io_device_info.compatible_id;
		/* Setup the table entries. */
		table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
		table[0].Name = L"CompatibleId";
		table[0].EntryContext = &keyValue; 
		table[0].DefaultType = REG_NONE;

		status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
			keyPath.Buffer, table, NULL, NULL );
		if( !NT_SUCCESS( status ) )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR ,
					( "Failed to read CompatibleId.\n" ) );
			goto _build_child_device_list_exit;
		}
		pNewDevList->io_device_info.compatible_id_size = keyValue.Length + 2*sizeof( WCHAR ); //2 null

		/* Get Description*/
		RtlInitUnicodeString( &keyValue, NULL );
		keyValue.MaximumLength = sizeof( pNewDevList->io_device_info.description );
		keyValue.Buffer = pNewDevList->io_device_info.description;

		/* Setup the table entries. */
		table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
		table[0].Name = L"Description";
		table[0].EntryContext = &keyValue; 
		table[0].DefaultType = REG_NONE;

		status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE, 
			keyPath.Buffer, table, NULL, NULL );
		if( !NT_SUCCESS( status ) )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Failed to read Description.\n" ) );
			goto _build_child_device_list_exit;
		}

		pNewDevList->io_device_info.description_size = keyValue.Length + sizeof( WCHAR );

		if( ( pNewDevList->io_device_info.description_size > MAX_DEVICE_ID_LEN) ||
		   ( pNewDevList->io_device_info.hardware_id_size > MAX_DEVICE_ID_LEN) ||
		   ( pNewDevList->io_device_info.compatible_id_size > MAX_DEVICE_ID_LEN) ||
		   ( pNewDevList->io_device_info.device_id_size > MAX_DEVICE_ID_LEN)
		   )
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Id or description size is too big.\n" ) );
			status = STATUS_UNSUCCESSFUL;
			goto _build_child_device_list_exit;
		}

		InitializeObjectAttributes( &objattr, &keyPath, OBJ_KERNEL_HANDLE, NULL, NULL );

		status = ZwOpenKey( &hParamKey, GENERIC_READ, &objattr );

		if ( !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "ZwOpenKey failed with status %u\n",status ) );
			goto _build_child_device_list_exit;
		}
			
		RtlInitUnicodeString( &valuename, L"IOCGUID" );
		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, NULL, 0, &sizeofbuf );

		if ( status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL ) 
		{
			pBuf = cl_zalloc( sizeofbuf );
		}

		if ( !pBuf && !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Either ZwQueryValueKey failed with status %u" 
					  "or cl_zalloc failed\n",status ) );
			goto _build_child_device_list_exit;
		}

		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, pBuf, sizeofbuf, &sizeofbuf );

		if ( !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "ZwQueryValueKey failed with status %u\n",status ) );
			goto _build_child_device_list_exit;				
		}

		pKeyVal = ( PKEY_VALUE_FULL_INFORMATION ) pBuf;

		if ( pKeyVal->Type & REG_BINARY ) 
		{
			memcpy( &guid_val, ( ( ( char * ) pBuf ) + pKeyVal->DataOffset ), sizeof( uint64_t ) );
			pNewDevList->io_device_info.ca_ioc_path.info.profile.ioc_guid = guid_val;
			guid_val = 0;
		} 
		else 
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "IOC GUID is not of REG_BINARY type\n" ) );
			goto _build_child_device_list_exit;				
		}
		cl_free( pBuf );
		pBuf = NULL;

		RtlInitUnicodeString( &valuename, L"CAGUID" );
		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, NULL, 0, &sizeofbuf );

		if ( status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL ) 
		{
			pBuf = cl_zalloc( sizeofbuf );
		}

		if ( !pBuf && !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Either ZwQueryValueKey failed with status %u"
					  "or cl_zalloc failed\n",status ) );
			goto _build_child_device_list_exit;				
		}

		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, pBuf, sizeofbuf, &sizeofbuf );

		if ( !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "ZwQueryValueKey failed with status %u\n",status ) );
			goto _build_child_device_list_exit;				
		}

		pKeyVal = ( PKEY_VALUE_FULL_INFORMATION ) pBuf;

		if ( pKeyVal->Type & REG_BINARY ) 
		{
			memcpy( &guid_val, ( ( ( char * ) pBuf ) + pKeyVal->DataOffset ), sizeof( uint64_t ) );
			pNewDevList->io_device_info.ca_ioc_path.ca_guid = guid_val;	
		} 
		else 
		{
			IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "CA GUID is not of REG_BINARY type\n"));
			goto _build_child_device_list_exit;				
		}

		cl_free( pBuf);
		pBuf = NULL;

		RtlInitUnicodeString( &valuename, L"InstanceId");
		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, NULL, 0, &sizeofbuf );

		if ( status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL ) 
		{
		pBuf = cl_zalloc( sizeofbuf );
		}

		if ( !pBuf && !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "Either ZwQueryValueKey failed with status %u" 
					  "or cl_zalloc failed\n",status ) );
			goto _build_child_device_list_exit;				
		}

		status = ZwQueryValueKey( hParamKey, &valuename, KeyValueFullInformation, 
				pBuf, sizeofbuf, &sizeofbuf );

		if ( !NT_SUCCESS( status ) ) 
		{
			IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR, 
					( "ZwQueryValueKey failed with status %u\n",status ) );
			goto _build_child_device_list_exit;				
		}

		pKeyVal = ( PKEY_VALUE_FULL_INFORMATION ) pBuf;

		memcpy( &instanceid, ( ( ( char * ) pBuf ) + pKeyVal->DataOffset ), sizeof( instanceid ) );
			pNewDevList->io_device_info.uniqueinstanceid = instanceid;

		cl_free( pBuf );
		pBuf = NULL;
			
		ZwClose( hParamKey );
		hParamKey = NULL;

		while( *curChild ) curChild++;
		curChild++;

	}


_build_child_device_list_exit:
	if( key_path_buffer )
	{
		cl_free( key_path_buffer );
	}

	if( !NT_SUCCESS( status ) )
	{
		_free_child_device_list();
	}

	if ( hParamKey != NULL ) 
	{
		ZwClose( hParamKey );
	}

	if ( pBuf ) 
	{
		cl_free( pBuf );
	}

	return status;
}

static NTSTATUS
__read_registry(
	IN				UNICODE_STRING* const		p_registry_path )
{
	NTSTATUS					status;
	/* Remember the terminating entry in the table below. */
	RTL_QUERY_REGISTRY_TABLE	table[3];
	UNICODE_STRING				param_path;

	IOU_ENTER( IOU_DBG_DRV );

	RtlInitUnicodeString( &param_path, NULL );
	param_path.MaximumLength = p_registry_path->Length + 
		sizeof(L"\\Parameters");
	param_path.Buffer = cl_zalloc( param_path.MaximumLength );
	if( !param_path.Buffer )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR,IOU_DBG_ERROR,
			("Failed to allocate parameters path buffer.\n") );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlAppendUnicodeStringToString( &param_path, p_registry_path );
	RtlAppendUnicodeToString( &param_path, L"\\Parameters" );

	/*
	 * Clear the table.  This clears all the query callback pointers,
	 * and sets up the terminating table entry.
	 */
	cl_memclr( table, sizeof(table) );

	/* Setup the table entries. */
	table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[0].Name = L"DebugLevel";
	table[0].EntryContext = &g_iou_dbg_level;
	table[0].DefaultType = REG_DWORD;
	table[0].DefaultData = &g_iou_dbg_level;
	table[0].DefaultLength = sizeof(ULONG);

	table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
	table[1].Name = L"DebugFlags";
	table[1].EntryContext = &g_iou_dbg_flags;
	table[1].DefaultType = REG_DWORD;
	table[1].DefaultData = &g_iou_dbg_flags;
	table[1].DefaultLength = sizeof(ULONG);
	/* Have at it! */
	status = RtlQueryRegistryValues( RTL_REGISTRY_ABSOLUTE,
		param_path.Buffer, table, NULL, NULL );

#ifndef EVENT_TRACING
	if( g_iou_dbg_flags & IOU_DBG_ERR )
		g_iou_dbg_flags |= CL_DBG_ERROR;
#endif

	if(!NT_SUCCESS( _build_child_device_list (&param_path) ) ){
	IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_DRV,
			 ( "Failed to create devices\n" ) );
	}


	IOU_PRINT( TRACE_LEVEL_INFORMATION, IOU_DBG_DRV,
		("debug level %d debug flags 0x%.8x\n",
		g_iou_dbg_level,
		g_iou_dbg_flags) );

	cl_free( param_path.Buffer );
	IOU_EXIT( IOU_DBG_DRV );
	return status;
}

static NTSTATUS
_create_child_ioc_device(
	IN				child_device_info_t	*p_child_dev )
{
	child_device_info_list_t	*p_prev_list, *p_new_dev_list, *temp = NULL;
	ca_ioc_map_t			*p_ca_ioc_map;
	ib_api_status_t			ib_status;

	p_prev_list = iou_globals.p_device_list;

	p_ca_ioc_map = find_ca_ioc_map( p_child_dev->ca_ioc_path.ca_guid,
					p_child_dev->ca_ioc_path.info.profile.ioc_guid );

	if ( p_ca_ioc_map == NULL ) 
	{
		return STATUS_INTERNAL_ERROR;
	}

	p_new_dev_list = cl_zalloc( sizeof( child_device_info_list_t ) );

	if ( !p_new_dev_list ) 
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR,
			      ( "Insufficient Memory\n" ) );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	p_new_dev_list->io_device_info = *p_child_dev;
	p_new_dev_list->next_device_info = NULL;

	if( p_prev_list == NULL ) 
	{
		iou_globals.p_device_list = p_new_dev_list;
	} 
	else 
	{
		temp = p_prev_list->next_device_info;
		p_prev_list->next_device_info = p_new_dev_list;
	}
	p_new_dev_list->next_device_info = temp;
	
	ib_status = _create_ioc_pdo( &p_new_dev_list->io_device_info, p_ca_ioc_map );

	if ( ib_status != IB_SUCCESS ) 
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IOU_DBG_ERROR,
			      ( "PDO creation for IOC failed\n" ) );
		cl_free( p_new_dev_list );
		return STATUS_INTERNAL_ERROR;
	}

	return STATUS_SUCCESS;
} 

static ULONG
_build_ca_ioc_list( ca_ioc_info_t	*p_info )
{
	cl_list_item_t	*p_item;
	ca_ioc_map_t	*p_map;

	p_item = cl_qlist_head( &iou_globals.ca_ioc_map_list );

	while ( p_item != cl_qlist_end( &iou_globals.ca_ioc_map_list ) ) 
	{
		p_map = ( ca_ioc_map_t	*) p_item;
		p_info->ca_guid = p_map->ca_guid;
		p_info->info = p_map->info;
		cl_memcpy( p_info->svc_entry_array, p_map->svc_entry_array, 
			sizeof( p_map->svc_entry_array ) );

		p_item = cl_qlist_next( p_item );
		p_info++;
	}

	return ( (ULONG) (sizeof(ca_ioc_info_t)*cl_qlist_count( &iou_globals.ca_ioc_map_list )) );
}

static NTSTATUS
iou_drv_open(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	IOU_ENTER( IOU_DBG_DRV );

	UNUSED_PARAM( p_dev_obj );

	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	/* We always succeed file handles creation. */
	p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	IOU_EXIT( IOU_DBG_DRV );
	return STATUS_SUCCESS;
	
}

static NTSTATUS
iou_drv_close(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	UNUSED_PARAM( p_dev_obj );

	p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	return STATUS_SUCCESS;	
}

static NTSTATUS
iou_drv_cleanup(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP					*p_irp)
{
	IOU_ENTER( IOU_DBG_DRV );

	UNUSED_PARAM( p_dev_obj );

	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	/*
	 * Note that we don't acquire the remove and stop lock on close to allow
	 * applications to close the device when the locks are already held.
	 */

	/* Complete the IRP. */
	p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	IOU_EXIT( IOU_DBG_DRV );
	return STATUS_SUCCESS;
}

static NTSTATUS
iou_drv_ioctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS		status = STATUS_SUCCESS;
	PIO_STACK_LOCATION	p_io_stack;
	ULONG			controlcode, n_bytes;
	size_t			n_iocs;
	ca_ioc_info_t		*p_ca_ioc_info;
	child_device_info_t	*p_child_dev;

	IOU_ENTER( IOU_DBG_DRV );
	CL_ASSERT( p_dev_obj );
	CL_ASSERT( p_irp );
	UNUSED_PARAM( p_dev_obj );

	/* Get the stack location. */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );
	controlcode = p_io_stack->Parameters.DeviceIoControl.IoControlCode;
	p_irp->IoStatus.Information = 0;

	switch ( controlcode ) 
	{
		case UAL_IOC_DEVICE_CREATE:
			p_child_dev = p_irp->AssociatedIrp.SystemBuffer;
			
			if ( sizeof( child_device_info_t) > 
				( p_io_stack->Parameters.DeviceIoControl.
				  InputBufferLength ) ) 
			{
				status = STATUS_BUFFER_TOO_SMALL;
			} 
			else 
			{
				status = _create_child_ioc_device( p_child_dev );
			}
			break;

		case UAL_IOC_LIST:
			p_ca_ioc_info = p_irp->AssociatedIrp.SystemBuffer;
			cl_mutex_acquire( &iou_globals.list_mutex );
			n_iocs = cl_qlist_count( &iou_globals.ca_ioc_map_list );

			if ( n_iocs*sizeof( ca_ioc_info_t) <= 
				p_io_stack->Parameters.DeviceIoControl.
				OutputBufferLength )
			{
				n_bytes = _build_ca_ioc_list( p_ca_ioc_info );
				p_irp->IoStatus.Information = n_bytes;
				status = STATUS_SUCCESS;
			} 
			else 
			{
				status = STATUS_BUFFER_TOO_SMALL;
			}

			cl_mutex_release( &iou_globals.list_mutex );

			break;
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	p_irp->IoStatus.Status = status;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	return status;
}

static NTSTATUS
iou_sysctl(
	IN				DEVICE_OBJECT				*p_dev_obj,
	IN				IRP							*p_irp )
{
	NTSTATUS		status;
	cl_pnp_po_ext_t	*p_ext;

	IOU_ENTER( IOU_DBG_DRV );

	CL_ASSERT( p_dev_obj );
	CL_ASSERT( p_irp );

	p_ext = p_dev_obj->DeviceExtension;

	if( p_ext->p_next_do )
	{
		IoSkipCurrentIrpStackLocation( p_irp );
		status = IoCallDriver( p_ext->p_next_do, p_irp );
	}
	else
	{
		status = p_irp->IoStatus.Status;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	}

	IOU_EXIT( IOU_DBG_DRV );
	return status;
}

static void
iou_unload(
	IN				DRIVER_OBJECT				*p_driver_obj )
{
	UNICODE_STRING	dos_name;

	IOU_ENTER( IOU_DBG_DRV );

#if defined(EVENT_TRACING)
	WPP_CLEANUP( p_driver_obj );
#else
	UNUSED_PARAM( p_driver_obj );
#endif

	CL_DEINIT;

	_free_child_device_list();

	RtlInitUnicodeString( &dos_name, 
				L"\\DosDevices\\Global\\VNICCONFIG" );
	IoDeleteSymbolicLink( &dos_name );
	

	IOU_EXIT( IOU_DBG_DRV );
}

static 
PDEVICE_OBJECT
initialize_ioctl_interface(
	IN PDRIVER_OBJECT	p_driver_obj )
{
	NTSTATUS	status;
	UNICODE_STRING	dev_name, dos_name;
	PDEVICE_OBJECT	p_dev_obj = NULL;

	RtlInitUnicodeString( &dev_name, L"\\Device\\VNICCONFIG" );
	RtlInitUnicodeString( &dos_name, 
			L"\\DosDevices\\Global\\VNICCONFIG" );

	status = IoCreateDevice( p_driver_obj, 0,
		&dev_name, FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN, FALSE, &p_dev_obj );

	if( !NT_SUCCESS( status ) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			( "Failed to create VNIC CONFIG device.\n" ) );
		return NULL;
	}

	IoDeleteSymbolicLink( &dos_name );

	status = IoCreateSymbolicLink( &dos_name, &dev_name );
	if(  !NT_SUCCESS( status ) )
	{
		IoDeleteDevice( p_dev_obj );
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			( "Failed to create symlink for dos name.\n" ) );
		p_dev_obj = NULL;
	}

	return p_dev_obj;
}

NTSTATUS
DriverEntry(
	IN				DRIVER_OBJECT				*p_driver_obj,
	IN				UNICODE_STRING				*p_registry_path )
{
	NTSTATUS			status;

	IOU_ENTER( IOU_DBG_DRV );

#if defined(EVENT_TRACING)
	WPP_INIT_TRACING( p_driver_obj, p_registry_path );
#endif

	status = CL_INIT;
	if( !NT_SUCCESS(status) )
	{
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("cl_init returned %08X.\n", status) );
		return status;
	}

	/* Store the driver object pointer in the global parameters. */
	iou_globals.p_driver_obj = p_driver_obj;
	iou_globals.p_device_list = NULL;

	/* Get the registry values. */
	status = __read_registry( p_registry_path );
	if( !NT_SUCCESS(status) )
	{
		CL_DEINIT;
		IOU_PRINT_EXIT( TRACE_LEVEL_ERROR, IOU_DBG_ERROR,
			("__read_registry returned %08x.\n", status) );
		return status;
	}

	/* Setup the entry points. */
	p_driver_obj->MajorFunction[IRP_MJ_CREATE] = iou_drv_open;
	p_driver_obj->MajorFunction[IRP_MJ_CLEANUP] = iou_drv_cleanup;
	p_driver_obj->MajorFunction[IRP_MJ_CLOSE] = iou_drv_close;

	p_driver_obj->MajorFunction[IRP_MJ_PNP] = cl_pnp;
	p_driver_obj->MajorFunction[IRP_MJ_POWER] = cl_power;
	p_driver_obj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = iou_sysctl;
	p_driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = iou_drv_ioctl;
	p_driver_obj->DriverUnload = iou_unload;
	p_driver_obj->DriverExtension->AddDevice = iou_add_device;

	iou_globals.p_config_device = initialize_ioctl_interface( p_driver_obj );

	cl_qlist_init( &iou_globals.ca_ioc_map_list );
	cl_mutex_init( &iou_globals.list_mutex );

	IOU_EXIT( IOU_DBG_DRV );
	return STATUS_SUCCESS;
}
