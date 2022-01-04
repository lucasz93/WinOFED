#include "l2w.h"
#include "ev_log.h"

#define MAX_BUFFER_SIZE		256

/* 
 * This function sends to Event Log messages with one WCHAR string and several binary parameters.
 * The string will be inserted instead of %2 parameter of the message.
 * Binary parameters will be shown in Dump Area of the message.
 * Binary parameters should be of type LONG.
 */
VOID
WriteEventLogEntryStr(
	PVOID	pi_pIoObject,
	ULONG	pi_ErrorCode,
	ULONG	pi_UniqueErrorCode,
	ULONG	pi_FinalStatus,
	PWCHAR	pi_InsertionStr,
	ULONG	pi_nDataItems,
	...
	)
/*++

Routine Description:
    Writes an event log entry to the event log.

Arguments:

	pi_pIoObject......... The IO object ( driver object or device object ).
	pi_ErrorCode......... The error code.
	pi_UniqueErrorCode... A specific error code.
	pi_FinalStatus....... The final status.
	pi_nDataItems........ Number of data items.
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
	int 	l_Size = (int)((pi_InsertionStr) ? ((wcslen(pi_InsertionStr) + 1) * sizeof( WCHAR )) : 0);
	int l_PktSize =sizeof(IO_ERROR_LOG_PACKET)+pi_nDataItems*sizeof(ULONG);
	int l_TotalSize =l_PktSize +l_Size;

	if (pi_pIoObject == NULL) {
		ASSERT(pi_pIoObject != NULL);
		return;
	}

	/* Init the variable argument list */   
	va_start(l_Argptr, pi_nDataItems);

	/* Allocate an error log entry */ 
	if (l_TotalSize >= ERROR_LOG_MAXIMUM_SIZE - 2) 
		l_TotalSize = ERROR_LOG_MAXIMUM_SIZE - 2;
	l_pErrorLogEntry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(
		pi_pIoObject,  (UCHAR)l_TotalSize );

	/* Check allocation */
	if ( l_pErrorLogEntry != NULL) 
	{ /* OK */

		/* Data item index */
		USHORT l_nDataItem ;

		/* Set the error log entry header */
		l_pErrorLogEntry->ErrorCode			= pi_ErrorCode; 
		l_pErrorLogEntry->DumpDataSize		= (USHORT) (pi_nDataItems*sizeof(ULONG)); 
		l_pErrorLogEntry->SequenceNumber	= 0; 
		l_pErrorLogEntry->MajorFunctionCode = 0; 
		l_pErrorLogEntry->IoControlCode		= 0; 
		l_pErrorLogEntry->RetryCount		= 0; 
		l_pErrorLogEntry->UniqueErrorValue	= pi_UniqueErrorCode; 
		l_pErrorLogEntry->FinalStatus		= pi_FinalStatus; 

		/* Insert the data items */
		for (l_nDataItem = 0; l_nDataItem < pi_nDataItems; l_nDataItem++) 
		{ /* Inset a data item */

			/* Current data item */
			int l_CurDataItem ;
				
			/* Get next data item */
			l_CurDataItem = va_arg( l_Argptr, int);

			/* Put it into the data array */
			l_pErrorLogEntry->DumpData[l_nDataItem] = l_CurDataItem ;

		} /* Inset a data item */

		/* add insertion string */
		if (pi_InsertionStr) {
			char *ptr; 
			int sz = min( l_TotalSize - l_PktSize, l_Size );
			l_pErrorLogEntry->NumberOfStrings = 1;
			l_pErrorLogEntry->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + l_pErrorLogEntry->DumpDataSize;
			ptr = (char*)l_pErrorLogEntry + l_pErrorLogEntry->StringOffset;
			memcpy( ptr, pi_InsertionStr, sz );
			*(WCHAR*)&ptr[sz - 2] = (WCHAR)0;
		}
		
		/* Write the packet */
		IoWriteErrorLogEntry(l_pErrorLogEntry);

	} /* OK */

	/* Term the variable argument list */   
	va_end(l_Argptr);

} /* WriteEventLogEntry */

/* 
 * This function sends to Event Log messages with various parameters.
 * Every parameter should be coded as a pair: a format specifier and the value.
 * 'pi_nDataItems' presents the number of the pairs.
 *
 * Here is an example:
 *
 * To print a message (from MC file) like:
 *
 *		MessageId=0x0006 Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_INFO_TEST
 *		Language=English
 *		some_long %2, some_short %3, some_byte %4, some_wide_char_str %5, some_ansii_str %6
 *
 * you have to code:
 *
 * 		WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_INFO_TEST, 0, 0, 5,
 *			L"%d", long_int,							// LONG
 *			L"%04x", (ULONG)short_int,					// SHORT
 *			L"%02x", (ULONG)byte_int,					// CHAR
 *			L"%s", wide_char_str,						// PWCHAR
 *			L"%S", ansii_str							// PCHAR
 *		);
 */
VOID
WriteEventLogEntryData(
	PVOID	pi_pIoObject,
	ULONG	pi_ErrorCode,
	ULONG	pi_UniqueErrorCode,
	ULONG	pi_FinalStatus,
	ULONG	pi_nDataItems,
	...
	)
/*++

Routine Description:
    Writes an event log entry to the event log.

Arguments:

	pi_pIoObject......... The IO object ( driver object or device object ).
	pi_ErrorCode......... The error code.
	pi_UniqueErrorCode... A specific error code.
	pi_FinalStatus....... The final status.
	pi_nDataItems........ Number of data items (i.e. pairs of data parameters).
	.
	. data items values
	.

Return Value:

	None .

--*/
{ /* WriteEventLogEntryData */

	/* Variable argument list */    
	va_list					l_Argptr;
	/* Pointer to an error log entry */
	PIO_ERROR_LOG_PACKET	l_pErrorLogEntry; 
	/* sizeof insertion string */
	int 	l_Size = 0;	
	/* temp buffer */
	UCHAR l_Buf[ERROR_LOG_MAXIMUM_SIZE - 2];
	/* position in buffer */
	UCHAR * l_Ptr = l_Buf;
	/* Data item index */
	USHORT l_nDataItem ;
	/* total packet size */
	int l_TotalSize;

	if (pi_pIoObject == NULL) {
		ASSERT(pi_pIoObject != NULL);
		return;
	}

	/* Init the variable argument list */   
	va_start(l_Argptr, pi_nDataItems);

	/* Create the insertion strings Insert the data items */
	memset( l_Buf, 0, sizeof(l_Buf) );
	for (l_nDataItem = 0; l_nDataItem < pi_nDataItems; l_nDataItem++) 
	{ 
		NTSTATUS status;
		/* Current binary data item */
		int l_CurDataItem ;
		/* Current pointer data item */
		void* l_CurPtrDataItem ;
		/* format specifier */
		WCHAR* l_FormatStr;
		/* the rest of the buffer */
		int l_BufSize = (int)(l_Buf + sizeof(l_Buf)- l_Ptr);
		/* size of insertion string */
		size_t l_StrSize;

		/* print as much as we can */
		if ( l_BufSize < 4 )
			break;
		
		/* Get format specifier */
		l_FormatStr = va_arg( l_Argptr, PWCHAR);
	
		/* Get next data item */
		if ( !wcscmp( l_FormatStr, L"%s" ) || !wcscmp( l_FormatStr, L"%S" ) ) {
			l_CurPtrDataItem = va_arg( l_Argptr, PWCHAR);
			/* convert to string */ 
			status = RtlStringCchPrintfW( (NTSTRSAFE_PWSTR)l_Ptr, l_BufSize>>1, l_FormatStr , l_CurPtrDataItem );
		}
		else {
			l_CurDataItem = va_arg( l_Argptr, int);
			/* convert to string */ 
			status = RtlStringCchPrintfW( (NTSTRSAFE_PWSTR)l_Ptr, l_BufSize>>1, l_FormatStr , l_CurDataItem );
		}

		if (!NT_SUCCESS(status))
			return;

		/* prepare the next loop */
		status = RtlStringCbLengthW( (NTSTRSAFE_PWSTR)l_Ptr, l_BufSize, &l_StrSize );
		if (!NT_SUCCESS(status))
			return;
		*(WCHAR*)&l_Ptr[l_StrSize] = (WCHAR)0;
		l_StrSize += 2;
		l_Size = l_Size + (int)l_StrSize;
		l_Ptr = l_Buf + l_Size;
		l_BufSize = (int)(l_Buf + sizeof(l_Buf)- l_Ptr);
	
	} /* Inset a data item */

	/* Term the variable argument list */   
	va_end(l_Argptr);

	/* Allocate an error log entry */ 
	l_TotalSize =sizeof(IO_ERROR_LOG_PACKET) +l_Size;
	if (l_TotalSize >= ERROR_LOG_MAXIMUM_SIZE - 2) {
		l_TotalSize = ERROR_LOG_MAXIMUM_SIZE - 2;
		l_Size = l_TotalSize - sizeof(IO_ERROR_LOG_PACKET);
	}
	l_pErrorLogEntry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(
		pi_pIoObject,  (UCHAR)l_TotalSize );

	/* Check allocation */
	if ( l_pErrorLogEntry != NULL) 
	{ /* OK */

		/* Set the error log entry header */
		l_pErrorLogEntry->ErrorCode			= pi_ErrorCode; 
		l_pErrorLogEntry->DumpDataSize		= 0; 
		l_pErrorLogEntry->SequenceNumber	= 0; 
		l_pErrorLogEntry->MajorFunctionCode = 0; 
		l_pErrorLogEntry->IoControlCode		= 0; 
		l_pErrorLogEntry->RetryCount		= 0; 
		l_pErrorLogEntry->UniqueErrorValue	= pi_UniqueErrorCode; 
		l_pErrorLogEntry->FinalStatus		= pi_FinalStatus; 
		l_pErrorLogEntry->NumberOfStrings = l_nDataItem;
		l_pErrorLogEntry->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + l_pErrorLogEntry->DumpDataSize;
		l_Ptr = (UCHAR*)l_pErrorLogEntry + l_pErrorLogEntry->StringOffset;
		if ( l_Size )
			memcpy( l_Ptr, l_Buf, l_Size );

		/* Write the packet */
		IoWriteErrorLogEntry(l_pErrorLogEntry);

	} /* OK */

} /* WriteEventLogEntry */

// bsize is to be a strlen(src)
// dest has to have enough place, i.e at least (2*strlen(src) + 2)
void __ansi_to_wchar( USHORT *dest, UCHAR *src, int bsize)
{
	int i;

	for (i=0; i<bsize; ++i)
		*dest++ = *src++;
	*dest = 0;
}

VOID
mlx4_err(
	IN struct mlx4_dev *	mdev,
	IN char*				format,
	...
	)
{
	va_list		list;
	UCHAR		buf[MAX_BUFFER_SIZE];
	WCHAR		wbuf[MAX_BUFFER_SIZE];

	// print to Debugger
	va_start(list, format);
	buf[MAX_BUFFER_SIZE - 1] = '\0';

	if (mdev == NULL) {
		ASSERT(mdev != NULL);
		return;
	}

	
	if (RtlStringCbVPrintfA( (char*)buf, sizeof(buf), format, list))
		return;
	cl_dbg_out( "%s\n", (char*)buf );
	va_end(list);

	// print to Event Log
	__ansi_to_wchar( wbuf, buf, (int)strlen((void*)buf) );
	WriteEventLogEntryStr( mdev->pdev->p_self_do, (ULONG)EVENT_MLX4_ANY_ERROR, 0, 0, wbuf, 0, 0 ); 
}

VOID
mlx4_dbg(
	IN struct mlx4_dev *	mdev,
	IN char*				format,
	...
	)
{
#if DBG
	va_list		list;
	UCHAR		buf[MAX_BUFFER_SIZE];
	UNUSED_PARAM(mdev);

	// print to Debugger
	va_start(list, format);
	buf[MAX_BUFFER_SIZE - 1] = '\0';
	RtlStringCbVPrintfA( (char*)buf, sizeof(buf), format, list);
	cl_dbg_out( "%s\n", (char*)buf );
	va_end(list);
#else	
	UNUSED_PARAM(mdev);
	UNUSED_PARAM(format);
#endif //DBG
}

VOID
dev_err(
	IN struct mlx4_dev **	mdev,
	IN char*				format,
	...
	)
{
	va_list		list;
	UCHAR		buf[MAX_BUFFER_SIZE];
	WCHAR		wbuf[MAX_BUFFER_SIZE];

	if (mdev == NULL) {
		ASSERT(mdev != NULL);
		return;
	}

	// print to Debugger
	va_start(list, format);
	buf[MAX_BUFFER_SIZE - 1] = '\0';
	RtlStringCbVPrintfA( (char*)buf, sizeof(buf), format, list);
	cl_dbg_out( "%s\n", (char*)buf );
	va_end(list);

	// print to Event Log
	RtlStringCchPrintfW(wbuf, sizeof(wbuf)/sizeof(wbuf[0]), L"%S", buf);
	WriteEventLogEntryStr( (*mdev)->pdev->p_self_do, (ULONG)EVENT_MLX4_ANY_ERROR, 0, 0, wbuf, 0, 0 ); 
}

VOID
dev_info(
	IN struct mlx4_dev **	p_mdev,
	IN char*				format,
	...
	)
{
#if DBG
	va_list		list;
	UCHAR		buf[MAX_BUFFER_SIZE];
	UNUSED_PARAM(p_mdev);

	// print to Debugger
	va_start(list, format);
	buf[MAX_BUFFER_SIZE - 1] = '\0';
	RtlStringCbVPrintfA( (char*)buf, sizeof(buf), format, list);
	cl_dbg_out( "%s\n", (char*)buf );
	va_end(list);
#else	
	UNUSED_PARAM(p_mdev);
	UNUSED_PARAM(format);
#endif
}



