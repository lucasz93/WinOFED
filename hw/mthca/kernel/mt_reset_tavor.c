#include <initguid.h>
#include <wdmguid.h>
#include "hca_driver.h"
#include "mthca.h"
#include "hca_debug.h"
#include "Mt_l2w.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_reset_tavor.tmh"
#endif


#pragma warning(disable : 4996)

/* limitations */
#define N_BUSES				16		/* max number of PCI buses */
#define N_DEVICES			32		/* max number of devices on one bus */
#define N_FUNCTIONS		8			/* max number of functions on one device */
#define N_CARDS				8			/* max number of HCA cards */

/*----------------------------------------------------------------*/

PWCHAR 
WcharFindChar(
	IN	PWCHAR 		pi_BufStart,
	IN	PWCHAR 		pi_BufEnd,
	IN	WCHAR		pi_FromPattern,
	IN	WCHAR		pi_ToPattern
	)
/*++

Routine Description:
    Converts wide-character string into ASCII

Arguments:

	pi_BufStart.......... start of the source string
	pi_BufEnd............ end of the source string
	pi_FromPattern....... start of pattern range to find
	pi_ToPattern......... end of pattern range to find

Return Value:

	pointer to the first pattern found or NULL (when reached the end)

--*/
{ /* WcharFindChar */

	PWCHAR	l_pResult	= pi_BufStart;

	while (l_pResult < pi_BufEnd )
	{
		if (*l_pResult >= pi_FromPattern && *l_pResult <= pi_ToPattern)
			return l_pResult;
		l_pResult++;
	}

	return NULL;

} /* WcharFindChar */


/*----------------------------------------------------------------*/

/*
 * Function: MdGetDevLocation
 *
 * Parameters:
 *		IN		pi_pPdo			- PDO of a device in question
 *		OUT	po_pBus			- pointer to the bus number of the device in question
 *		OUT	po_pDevFunc	- pointer to dev/func of the device, if found
 * 
 * Returns:
 *		not STATUS_SUCCESS	- the device location was not found
 * 	STATUS_SUCCESS 		- the device location was found and returned in OUT parameters
 *
 * Description:
 *		The function uses IoGetDeviceProperty to get the location of a device with given PDO
 *			
 */
static NTSTATUS 
MdGetDevLocation(
	IN 	PDEVICE_OBJECT 	pi_pPdo,
	OUT	ULONG *			po_pBus,
	OUT ULONG	 *			po_pDevFunc 
	)
{
	ULONG	l_BusNumber, l_DevNumber, l_Function, l_ResultLength = 0;
	WCHAR	l_Buffer[40], *l_pEnd, *l_pBuf = l_Buffer, *l_pBufEnd = l_Buffer + sizeof(l_Buffer);
	NTSTATUS	l_Status;
	UNICODE_STRING	l_UnicodeNumber;

	/* prepare */
    	l_ResultLength = 0;
	RtlZeroMemory( l_Buffer, sizeof(l_Buffer) );

	/* Get the device number  */
	l_Status = IoGetDeviceProperty(pi_pPdo,
		DevicePropertyLocationInformation, sizeof(l_Buffer), l_Buffer, &l_ResultLength);

	/* Verify if the function was successful */
	if ( !NT_SUCCESS(l_Status) || !l_ResultLength ) {
		HCA_PRINT( TRACE_LEVEL_ERROR  ,HCA_DBG_SHIM  ,("(MdGetDevLocation) Unable to get device number: Status 0x%x, ResultSize %d \n", 
			l_Status, l_ResultLength  ));
		goto exit;	
	}

	// ALL THE BELOW CRAP WE DO INSTEAD OF 
	// sscanf(l_Buffer, "PCI bus %d, device %d, function %d", &l_BusNumber, &l_DevNumber, &l_Function );

	/* take bus number */
	l_pBuf	= WcharFindChar( l_pBuf, l_pBufEnd, L'0', L'9' );
	if (l_pBuf == NULL) goto err;
	l_pEnd	= WcharFindChar( l_pBuf, l_pBufEnd, L',', L',' );
	if (l_pEnd == NULL) goto err;
	l_UnicodeNumber.Length = l_UnicodeNumber.MaximumLength = (USHORT)((PCHAR)l_pEnd - (PCHAR)l_pBuf);
	l_UnicodeNumber.Buffer = l_pBuf; l_pBuf = l_pEnd;
	RtlUnicodeStringToInteger( &l_UnicodeNumber, 10, &l_BusNumber);

	/* take slot number */
	l_pBuf	= WcharFindChar( l_pBuf, l_pBufEnd, L'0', L'9' );
	if (l_pBuf == NULL) goto err;
	l_pEnd	= WcharFindChar( l_pBuf, l_pBufEnd, L',', L',' );
	if (l_pEnd == NULL) goto err;
	l_UnicodeNumber.Length = l_UnicodeNumber.MaximumLength = (USHORT)((PCHAR)l_pEnd - (PCHAR)l_pBuf);
	l_UnicodeNumber.Buffer = l_pBuf; l_pBuf = l_pEnd;
	RtlUnicodeStringToInteger( &l_UnicodeNumber, 10, &l_DevNumber);

	/* take function number */
	*(l_Buffer + (l_ResultLength>>1)) = 0;	/* set end of string */
	l_pBuf	= WcharFindChar( l_pBuf, l_pBufEnd, L'0', L'9' );
	if (l_pBuf == NULL) goto err;
	l_pEnd	= WcharFindChar( l_pBuf, l_pBufEnd, 0, 0 );
	if (l_pEnd == NULL) goto err;
	l_UnicodeNumber.Length = l_UnicodeNumber.MaximumLength = (USHORT)((PCHAR)l_pEnd - (PCHAR)l_pBuf);
	l_UnicodeNumber.Buffer = l_pBuf; l_pBuf = l_pEnd;
	RtlUnicodeStringToInteger( &l_UnicodeNumber, 10, &l_Function);

	/* return the results */
	*po_pBus		= l_BusNumber;
	*po_pDevFunc = (l_DevNumber & 0x01f) | ((l_Function & 7) << 5);

	goto exit;

err:
	l_Status = STATUS_UNSUCCESSFUL;
exit:
	return l_Status;
}

/*----------------------------------------------------------------*/

/* Function: SendAwaitIrpCompletion
 *		
 *  Parameters:
 *
 *  Description:
 *		IRP completion routine 
 *
 *  Returns:
 *		pointer to the entry on SUCCESS
 *		NULL - otherwise
 *
*/ 
static
NTSTATUS
SendAwaitIrpCompletion (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
{
    UNREFERENCED_PARAMETER (DeviceObject);    
    UNREFERENCED_PARAMETER (Irp);    
    KeSetEvent ((PKEVENT) Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

/*------------------------------------------------------------------------------------------------------*/

/*
 *  Function: SendAwaitIrp
 *
 *  Description:
 *		Create and send IRP stack down the stack and wait for the response (Blocking Mode)
 *
 *  Parameters:
 *		pi_pDeviceExt.......... ointer to USB device extension
 *		pi_MajorCode........... IRP major code
 *		pi_MinorCode........... IRP minor code
 *		pi_pBuffer............. parameter buffer
 *		pi_nSize............... size of the buffer
 *    po_pInfo.............. returned field Information from IoStatus block
 *
 *  Returns:
 *		pointer to the entry on SUCCESS
 *		NULL - otherwise
 *
*/
static 
NTSTATUS 
SendAwaitIrp(
	IN  PDEVICE_OBJECT		pi_pFdo,
	IN  PDEVICE_OBJECT		pi_pLdo,
	IN  ULONG				pi_MajorCode,
	IN  ULONG				pi_MinorCode,
	IN	PVOID				pi_pBuffer,
	IN	int					pi_nSize,
	OUT	PVOID		*		po_pInfo
   )
/*++

 Routine Description: 

 	Create and send IRP stack down the stack and wait for the response (
Blocking Mode)

 Arguments: 
 
	pi_pFdo................ our device
	pi_pLdo................ lower device
	pi_MajorCode........... IRP major code
	pi_MinorCode........... IRP minor code
	pi_pBuffer............. parameter buffer
	pi_nSize............... size of the buffer

 Returns: 
 
	standard NTSTATUS return codes.

 Notes:

--*/
{ /* SendAwaitIrp */
	// Event
	KEVENT				l_hEvent;
	// Pointer to IRP
	PIRP				l_pIrp;
	// Stack location
	PIO_STACK_LOCATION	l_pStackLocation;
	// Returned status
	NTSTATUS			l_Status;
	// when to invoke
	BOOLEAN InvokeAlways = TRUE;

	// call validation
	if(KeGetCurrentIrql() != PASSIVE_LEVEL)
		return STATUS_SUCCESS;

	// create event
	KeInitializeEvent(&l_hEvent, NotificationEvent, FALSE);

	// build IRP request to USBD driver
	l_pIrp = IoAllocateIrp( pi_pFdo->StackSize, FALSE );

	// validate request
	if (!l_pIrp)
	{
	    //MdKdPrint( DBGLVL_MAXIMUM, ("(SendAwaitIrp) Unable to allocate IRP !\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// fill IRP
	l_pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	// set completion routine
    IoSetCompletionRoutine(l_pIrp,SendAwaitIrpCompletion, &l_hEvent, InvokeAlways, InvokeAlways, InvokeAlways);

	// fill stack location
    l_pStackLocation = IoGetNextIrpStackLocation(l_pIrp);
    l_pStackLocation->MajorFunction= (UCHAR)pi_MajorCode;
    l_pStackLocation->MinorFunction= (UCHAR)pi_MinorCode;
	RtlCopyMemory( &l_pStackLocation->Parameters, pi_pBuffer, pi_nSize );

	// Call lower driver perform request
	l_Status = IoCallDriver( pi_pLdo, l_pIrp ); 

	// if the request not performed --> wait
	if (l_Status == STATUS_PENDING)
	{
		// Wait until the IRP  will be complete
		KeWaitForSingleObject(
			&l_hEvent, 								// event to wait for
			Executive, 								// thread type (to wait into its context)
			KernelMode, 							// mode of work
			FALSE, 									// alertable
			NULL									// timeout
		);
		l_Status = l_pIrp->IoStatus.Status;
	}

	if (po_pInfo)
		*po_pInfo = (PVOID)l_pIrp->IoStatus.Information;

    IoFreeIrp(l_pIrp);
 	return l_Status;

} /* SendAwaitIrp */


/*------------------------------------------------------------------------------------------------------*/

/*
 * Function: FindBridgeIf_new
 *
 * Parameters:
 *		IN		pi_pPdo			- PDO of HCA's bus device
 *		IN 	pi_Bus, pi_DevFunc	- bridge location
 *		OUT	po_pPdo			- pointer to PDO of the bridge, when found
 * 
 * Returns:
 *		FALSE	- the bridge was not found
 * 	TRUE 	- a device was found;  *po_pPdo contains its PDO
 *
 * Description:
 *		The function finds and opens the bus interface for Tavor HCA
 *
 * Algorithm:
 *	1. find all PDOs of PCI.SYS driver and save it into an array;
 *	2. For each PDO open its bus i/f and check whether it is our bridge;
 *
 *	Note:
 *		1. It is a "hack" algorithm. It uses some fields of system structures and some
 *		optimistic assumptions - see more below
 *		2. We dangerously assume, that during part to of the algoritm no PDO will removed or added !
 *		3. PCI.SYS gives to its child devices names like \Device\NTPNP_PCI00nn. I tried to get Bridge's
 *		PDO by calling IoGetDeviceObjectPointer with all such names, but it returns STATUS_NO_SUCH_DEVICE
 *		for the *right* name of Bridge device !(IoGetDeviceObjectPointer really opens the device. Maybe Bridge is in exclusive use)
 */
int
FindBridgeIf(
	IN hca_dev_ext_t		*pi_ext,
	OUT PBUS_INTERFACE_STANDARD	pi_pInterface
	)
{
	NTSTATUS l_Status;
	int rc = FALSE;	/* result - "not found" by default */
	int n_pdos = 0;			/* number of PCI.SYS's PDOs */
	PDEVICE_OBJECT *pdo;	/* an array of PCI.SYS's PDOs */
	PDEVICE_OBJECT l_pHcaPdo;

	{ // get HCA's bus PDO
		IO_STACK_LOCATION l_Iosl;
		PDEVICE_RELATIONS l_pDr;

		// find PDO of our bus driver (bypassing possible low filter drivers)
		RtlZeroMemory( &l_Iosl, sizeof(l_Iosl) );
		l_Iosl.Parameters.QueryDeviceRelations.Type = TargetDeviceRelation;
		l_Status = SendAwaitIrp(
			pi_ext->cl_ext.p_self_do,
			pi_ext->cl_ext.p_next_do,
			IRP_MJ_PNP,
			IRP_MN_QUERY_DEVICE_RELATIONS,
			&l_Iosl.Parameters,
			sizeof(l_Iosl.Parameters.QueryDeviceRelations),
			&l_pDr
			 );
		
		if (!NT_SUCCESS (l_Status)) {
			HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,("IRP_MN_QUERY_DEVICE_RELATIONS for TargetDeviceRelation failed (%#x);: Fdo %p, Ldo %p \n",
				l_Status, pi_ext->cl_ext.p_self_do, pi_ext->cl_ext.p_next_do ));
			goto exit;
		}

		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_SHIM ,("IRP_MN_QUERY_DEVICE_RELATIONS for TargetDeviceRelation for Fdo %p, Ldo %p: num_of_PDOs %d, PDO %p \n",
			pi_ext->cl_ext.p_self_do, pi_ext->cl_ext.p_next_do, l_pDr->Count, l_pDr->Objects[0] ));
		l_pHcaPdo = l_pDr->Objects[0];
	}

	{ // allocate and fill an array with all PCI.SYS PDO devices
		// suppose that there is no more than N_PCI_DEVICES, belonging to PCI.SYS
		#define N_PCI_DEVICES	256
		KIRQL irql;
		PDRIVER_OBJECT l_pDrv;
		PDEVICE_OBJECT l_pPdo;
		int l_all_pdos = 0;
	
		pdo = (PDEVICE_OBJECT *)ExAllocatePoolWithTag(
			NonPagedPool,
			N_PCI_DEVICES * sizeof(PDEVICE_OBJECT),
			MT_TAG_KERNEL );
		if (!pdo)
			goto exit;
		
		// suppose, that PDOs are added only at PASSIVE_LEVEL
		irql = KeRaiseIrqlToDpcLevel();
			
		// get to the PCI.SYS driver
		l_pDrv = l_pHcaPdo->DriverObject;

		// find and store all bus PDO	s (because the bridge is a bus enumerated device)
		for ( l_pPdo = l_pDrv->DeviceObject; l_pPdo; l_pPdo = l_pPdo->NextDevice ) {
			l_all_pdos++;
			if ( l_pPdo->Flags & DO_BUS_ENUMERATED_DEVICE ) {
				pdo[n_pdos] = l_pPdo;
				if (++n_pdos >= N_PCI_DEVICES) {
					HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_SHIM ,
						("There are more than %d children of PCI.SYS. Skipping the rest \n", N_PCI_DEVICES ));
					break;
				}
			}
		}

		// return to previous level
		KeLowerIrql(irql);
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_SHIM ,("Found %d PCI.SYS's PDOs (from %d) \n", n_pdos, l_all_pdos ));
	}

	{ // Find PDO of the Bridge of our HCA and return open bus interface to it
		int i;
		ULONG data, l_SecBus;
		IO_STACK_LOCATION l_Stack; // parameter buffer for the request
		ULONG l_DevId = ((int)(23110) << 16) | PCI_VENDOR_ID_MELLANOX;	

		//  loop over all the PCI driver devices
		for ( i = 0; i < n_pdos; ++i ) {

			// clean interface data
			RtlZeroMemory( (PCHAR)pi_pInterface, sizeof(BUS_INTERFACE_STANDARD) );
			
			// get Bus Interface for the current PDO
			l_Stack.Parameters.QueryInterface.InterfaceType 		= (LPGUID) &GUID_BUS_INTERFACE_STANDARD;
			l_Stack.Parameters.QueryInterface.Size					= sizeof(BUS_INTERFACE_STANDARD);
			l_Stack.Parameters.QueryInterface.Version				= 1;
			l_Stack.Parameters.QueryInterface.Interface 			= (PINTERFACE)pi_pInterface;
			l_Stack.Parameters.QueryInterface.InterfaceSpecificData = NULL;
			
			l_Status =SendAwaitIrp( pi_ext->cl_ext.p_self_do, pdo[i], IRP_MJ_PNP, 
				IRP_MN_QUERY_INTERFACE, &l_Stack.Parameters, sizeof(l_Stack.Parameters), NULL);
			if (!NT_SUCCESS (l_Status)) {
				HCA_PRINT( TRACE_LEVEL_WARNING  ,HCA_DBG_SHIM  ,
					("Failed to get bus interface for pdo[%d] %p, error %#x \n", i, pdo[i], l_Status ));
				continue;
			}

			// Read DevID
			data = 0;
			if (4 != pi_pInterface->GetBusData( pi_pInterface->Context,
				PCI_WHICHSPACE_CONFIG, &data, 0, 4)) {
				HCA_PRINT( TRACE_LEVEL_WARNING, HCA_DBG_PNP, 
					("Failed to read DevID for pdo[%d] %p, data %#x \n", i, pdo[i], data ));
				goto next_loop;
			}

			if (data != l_DevId) {
				HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
					("Not Tavor bridge: pdo[%d] %p, data %#x \n", i, pdo[i], data ));
				goto next_loop;
			}

			// Found Tavor Bridge - read its SecondaryBus
			data = 0;
			if (4 != pi_pInterface->GetBusData( pi_pInterface->Context,
				PCI_WHICHSPACE_CONFIG, &data, 24, 4)) { /* 24 - PrimaryBus, 25 - SecondaryBus, 26 - SubordinateBus */
				HCA_PRINT( TRACE_LEVEL_WARNING, HCA_DBG_PNP, 
					("Failed to read SecondaryBus for pdo[%d] %p, data %#x \n", i, pdo[i], data ));
				goto next_loop;
			}

			l_SecBus = (data >> 16) & 255;
			if (l_SecBus != pi_ext->bus_number) {
				HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
					("Wrong bridge for our HCA: pdo[%d] %p, SecBus %d, HcaBus %d \n", i, pdo[i], l_SecBus, pi_ext->bus_number ));
				goto next_loop;
			}
			else {
				ULONG l_DevFunc, l_Bus;
				l_Status = MdGetDevLocation( pdo[i], &l_Bus, &l_DevFunc );
				HCA_PRINT( TRACE_LEVEL_INFORMATION, HCA_DBG_PNP, 
					("Found bridge for our HCA: pdo[%d] %p (bus %d, dev/func %d, HcaPdo %p), SecBus %d, HcaBus %d \n", 
					i, pdo[i], l_Bus, l_DevFunc, l_pHcaPdo, l_SecBus, pi_ext->bus_number ));
				rc = TRUE;
				break;
			}
		next_loop:	
			pi_pInterface->InterfaceDereference( pi_pInterface->Context );
		}
	}

	ExFreePool(pdo);
exit:	
	return rc;	
}
