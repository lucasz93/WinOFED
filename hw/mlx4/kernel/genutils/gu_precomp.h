#pragma warning(disable:4214)   // bit field types other than int
#pragma warning(disable:4201)   // nameless struct/union
#pragma warning(disable:4115)   // named type definition in parentheses
#pragma warning(disable:4127)   // conditional expression is constant
#pragma warning(disable:4054)   // cast of function pointer to PVOID
#pragma warning(disable:4206)   // translation unit is empty
#pragma warning(disable:4100)   // unreferenced formal parameter


//extern "C" {
#include <ntddk.h>
#include <wdm.h>
//#include <wdf.h>
#include <wdmsec.h>
//}


#include <ntintsafe.h>


#include <ntstatus.h>
#include <initguid.h>
#include <stdio.h>
#include <WinDef.h>
#include <ntstrsafe.h>
#ifndef NTDDI_WIN8
#include <strsafe.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
extern "C" {
#include <ndis.h>
#include <Netioapi.h>
}

#include "gu_defs.h"
#include "gu_dbg.h"
#include "gu_wpptrace.h"
#include "gu_utils.h"
#include "gu_address_translation.h"
#include "gu_affinity.h"

#include "shutter.h"


