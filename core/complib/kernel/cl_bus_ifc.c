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




#include <complib/cl_bus_ifc.h>


/* Forwards the request to the HCA's PDO. */
NTSTATUS
cl_fwd_query_ifc(
	IN				DEVICE_OBJECT* const		p_dev_obj,
	IN				IO_STACK_LOCATION* const	p_io_stack )
{
	NTSTATUS			status;
	IRP					*p_irp;
	IO_STATUS_BLOCK		io_status;
	IO_STACK_LOCATION	*p_fwd_io_stack;
	DEVICE_OBJECT		*p_target;
	KEVENT				event;
	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
	CL_ASSERT( p_io_stack->MinorFunction == IRP_MN_QUERY_INTERFACE );

	p_target = IoGetAttachedDeviceReference( p_dev_obj );

	KeInitializeEvent( &event, NotificationEvent, FALSE );

	/* Build the IRP for the HCA. */
	p_irp = IoBuildSynchronousFsdRequest( IRP_MJ_PNP, p_target,
		NULL, 0, NULL, &event, &io_status );
	if( !p_irp )
	{
		ObDereferenceObject( p_target );
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* Copy the request query parameters. */
	p_fwd_io_stack = IoGetNextIrpStackLocation( p_irp );
	p_fwd_io_stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
	p_fwd_io_stack->Parameters.QueryInterface =
		p_io_stack->Parameters.QueryInterface;
	p_irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	/* Send the IRP. */
	status = IoCallDriver( p_target, p_irp );
	if( status == STATUS_PENDING )
	{
		KeWaitForSingleObject( &event, Executive, KernelMode,
			FALSE, NULL );

		status = io_status.Status;
	}
#ifdef PRINT_QUERY_INTERFACE
	DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"cl_fwd_query_ifc: p_dev_obj %p, status %#x, guid %I64x\n", 
		p_target, status, guid );
#endif
	ObDereferenceObject( p_target );

    if( NT_SUCCESS( status ) )
    {
        if( p_io_stack->Parameters.QueryInterface.Version != p_io_stack->Parameters.QueryInterface.Interface->Version )
        {        
            p_io_stack->Parameters.QueryInterface.Interface->InterfaceDereference(p_io_stack->Parameters.QueryInterface.Interface->Context);
            status = STATUS_NOT_SUPPORTED;
        }        
    }

	return status;
}
