/*
 * Copyright (c) 2005 Mellanox Technologies LTD.  All rights reserved.
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
 *
 */

// Author: Yossi Leybovich 

#include "hca_driver.h"


VOID
WriteEventLogEntry(
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
	pi_nDataItems........ Number of data items.
	.
	. data items values
	.

Return Value:

	None .

--*/
{ /* WriteEventLogEntry */

	/* Variable argument list */    
	va_list					l_Argptr;
	/* Pointer to an error log entry */
	PIO_ERROR_LOG_PACKET	l_pErrorLogEntry; 

	/* Init the variable argument list */   
	va_start(l_Argptr, pi_nDataItems);

	if(pi_pIoObject == NULL) {
		return;
	}
	
	/* Allocate an error log entry */ 
    l_pErrorLogEntry = 
	(PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(
								pi_pIoObject,
								(UCHAR)(sizeof(IO_ERROR_LOG_PACKET)+pi_nDataItems*sizeof(ULONG))
								); 
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

        /* Write the packet */
		IoWriteErrorLogEntry(l_pErrorLogEntry);

    } /* OK */

	/* Term the variable argument list */   
	va_end(l_Argptr);

} /* WriteEventLogEntry */

/*------------------------------------------------------------------------------------------------------*/

VOID
WriteEventLogEntryStr(
	PVOID	pi_pIoObject,
	ULONG	pi_ErrorCode,
	ULONG	pi_UniqueErrorCode,
	ULONG	pi_FinalStatus,
	PWCHAR pi_InsertionStr,
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

	if(pi_pIoObject == NULL) {
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






