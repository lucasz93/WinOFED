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

#include <stdarg.h>
#include "complib/cl_log.h"

WCHAR g_cl_wlog[ CL_LOG_BUF_LEN ]; 
UCHAR g_cl_slog[ CL_LOG_BUF_LEN ]; 


VOID
cl_event_log_write(
	PVOID	p_io_object,
	ULONG	p_error_code,
	ULONG	p_unique_error_code,
	ULONG	p_final_status,
	PWCHAR	p_insertion_string,
	ULONG	p_n_data_items,
	...
	)
/*++

Routine Description:
    Writes an event log entry to the event log.

Arguments:

	p_io_object......... The IO object ( driver object or device object ).
	p_error_code......... The error code.
	p_unique_error_code... A specific error code.
	p_final_status....... The final status.
	p_n_data_items........ Number of data items.
	.
	. data items values
	.

Return Value:

	None .

--*/
{ /* WriteEventLogEntryStr */

	/* Variable argument list */    
	va_list					l_Argptr;
	/* Pointer to an error log entry */
	PIO_ERROR_LOG_PACKET	l_pErrorLogEntry; 
	/* sizeof insertion string */
	int 	l_Size = (int)((p_insertion_string) ? ((wcslen(p_insertion_string) + 1) * sizeof( WCHAR )) : 0);
	int l_PktSize =sizeof(IO_ERROR_LOG_PACKET)+p_n_data_items*sizeof(ULONG);
	int l_TotalSize =l_PktSize +l_Size;

	if (p_io_object == NULL) {
		ASSERT(p_io_object != NULL);
		return;
	}

	/* Init the variable argument list */   
	va_start(l_Argptr, p_n_data_items);

	/* Allocate an error log entry */ 
	if (l_TotalSize >= ERROR_LOG_MAXIMUM_SIZE - 2) 
		l_TotalSize = ERROR_LOG_MAXIMUM_SIZE - 2;
	l_pErrorLogEntry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(
		p_io_object,  (UCHAR)l_TotalSize );

	/* Check allocation */
	if ( l_pErrorLogEntry != NULL) 
	{ /* OK */

		/* Data item index */
		USHORT l_nDataItem ;

		/* Set the error log entry header */
		l_pErrorLogEntry->ErrorCode			= p_error_code; 
		l_pErrorLogEntry->DumpDataSize		= (USHORT) (p_n_data_items*sizeof(ULONG)); 
		l_pErrorLogEntry->SequenceNumber	= 0; 
		l_pErrorLogEntry->MajorFunctionCode = 0; 
		l_pErrorLogEntry->IoControlCode		= 0; 
		l_pErrorLogEntry->RetryCount		= 0; 
		l_pErrorLogEntry->UniqueErrorValue	= p_unique_error_code; 
		l_pErrorLogEntry->FinalStatus		= p_final_status; 

		/* Insert the data items */
		for (l_nDataItem = 0; l_nDataItem < p_n_data_items; l_nDataItem++) 
		{ /* Inset a data item */

			/* Current data item */
			int l_CurDataItem ;
				
			/* Get next data item */
			l_CurDataItem = va_arg( l_Argptr, int);

			/* Put it into the data array */
			l_pErrorLogEntry->DumpData[l_nDataItem] = l_CurDataItem ;

		} /* Inset a data item */

		/* add insertion string */
		if (p_insertion_string) {
			char *ptr; 
			int sz = min( l_TotalSize - l_PktSize, l_Size );
			l_pErrorLogEntry->NumberOfStrings = 1;
			l_pErrorLogEntry->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + l_pErrorLogEntry->DumpDataSize;
			ptr = (char*)l_pErrorLogEntry + l_pErrorLogEntry->StringOffset;
			memcpy( ptr, p_insertion_string, sz );
			*(WCHAR*)&ptr[sz - 2] = (WCHAR)0;
		}
		
		/* Write the packet */
		IoWriteErrorLogEntry(l_pErrorLogEntry);

	} /* OK */

	/* Term the variable argument list */   
	va_end(l_Argptr);

} /* WriteEventLogEntry */


/*
 * The IO Object required to allocate an event log entry is passed in
 * via the "name" parameter.
 */
void
cl_log_event(
	IN	const char* const	name,
	IN	const cl_log_type_t	type,
	IN	const char* const	p_message,
	IN	const void* const	p_data,
	IN	const uint32_t		data_len )
{
	UNUSED_PARAM( name );
	UNUSED_PARAM( type );
	UNUSED_PARAM( p_message );
	UNUSED_PARAM( p_data );
	UNUSED_PARAM( data_len );
	/* 
	 * To log errors requires setting up custom error strings and registering 
	 * them with the system.  Do this later.
	 */
	//IO_ERROR_LOG_PACKET		*p_entry;
	//size_t					size = sizeof(IO_ERROR_LOG_PACKET);
	//UCHAR					*p_dump_data;
	//WCHAR					*p_str;

	//if( p_message )
	//	size += strlen( p_message );

	//size += data_len;

	//if( size > ERROR_LOG_MAXIMUM_SIZE )
	//	return;

	//p_entry = IoAllocateErrorLogEntry( name, (UCHAR)size );
	//if( !p_entry )
	//	return;

	//cl_memclr( p_entry, size );

	//p_dump_data = p_entry->DumpData;

	///* Copy the string to the dump data. */
	//if( p_message )
	//{
	//	cl_memcpy( p_dump_data, p_message, strlen( p_message ) + 1 );
	//	p_dump_data += strlen( p_message ) + 1;
	//}

	//if( data_len )
	//	cl_memcpy( p_dump_data, p_data, data_len );

	//switch( type )
	//{
	//case CL_LOG_ERROR:
	//	p_entry->ErrorCode = STATUS_UNSUCCESSFUL;
	//	break;

	//case CL_LOG_WARN:
	//	p_entry->ErrorCode = STATUS_UNSUCCESSFUL;
	//	break;

	//default:
	//case CL_LOG_INFO:
	//	p_entry->ErrorCode = STATUS_SERVICE_NOTIFICATION;
	//	break;
	//}
}
