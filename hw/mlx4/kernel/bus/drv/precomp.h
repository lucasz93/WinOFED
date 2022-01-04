#pragma warning(disable:4201)   // nameless struct/union

#include <ntddk.h>
#include <wdf.h>
#include <ndioctl.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include <initguid.h> // required for GUID definitions

#include "gu_defs.h"
#include "gu_utils.h"
#include "gu_affinity.h"

#include "public.h"
#include "l2w.h"
#include "vip_dev.h"
#include "drv.h"
#include "driver.h"
#include "cmd.h"
#include <mlx4_debug.h>
#include "ib\mlx4_ib.h"
#include "stat.h"

