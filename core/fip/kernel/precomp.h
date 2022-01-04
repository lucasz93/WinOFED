
#pragma warning(disable:4214)   // bit field types other than int

#pragma warning(disable:4201)   // nameless struct/union
#pragma warning(disable:4115)   // named type definition in parentheses
#pragma warning(disable:4127)   // conditional expression is constant
#pragma warning(disable:4054)   // cast of function pointer to PVOID
#pragma warning(disable:4206)   // translation unit is empty

extern "C" {
//#include <ndis.h>
#include <ntddk.h>

#include <wdf.h>
#include <WdfMiniport.h>
#include <Netioapi.h>
}

#ifdef NTDDI_WIN8
#include <ntstrsafe.h>
#else
#include <strsafe.h>
#endif
#include <ntstatus.h>
#include <ntintsafe.h>



#include "gu_defs.h"
#include "gu_dbg.h"
#include "gu_utils.h"
//#include "gu_ndis_utils.h"
#include "gu_timer.h"
#include "gu_address_translation.h"

extern "C" {

#include <iba/ib_al.h>
#include <al_mcast_mgr.h>
#include "al_ci_ca.h"
#include "al_mgr.h"
#include "al_dev.h"
#include "iba/ipoib_ifc.h"
#include "l2w.h"    // For Get-Bus-IF
}

#include "bus_intf.h"    // For Get-Bus-IF
#include "public.h"      // For Get-Bus-IF

#include "fip_debug.h"
#include "send_recv.h"
#include "fip_utils.h"
#include "fip_pkt.h"
#include "fip_eoib_interface.h"


#include "fip_main.h"
#include "fip_messages.h"
#include "fip_vhub_table.h"
#include "fip_vnic.h"
#include "fip_gw.h"
#include "fip_port.h"
#include "fip_thread.h"




