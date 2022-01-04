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



#ifndef _SRP_DATA_H_
#define _SRP_DATA_H_


//#define SRP_SCSI_MINIPORT


#include <complib/cl_types.h>
#pragma warning( push, 3 )
//#if WINVER == 0x500 || defined( SRP_SCSI_MINIPORT )
//#include <srb.h>
//#include <scsi.h>
//#else   /* WINVER == 0x500 */

// WinXP typo workaround
#if defined (WinXP)
#define RaidPortReady StorPortReady
#endif

#include <storport.h>
//#endif  /* WINVER == 0x500 */
#pragma warning( pop )

#define SRP_OBJ_TYPE_DRV        0x10000000
#define SRP_OBJ_TYPE_HBA        0x10000001
#define SRP_OBJ_TYPE_SESSION    0x10000002


/* Device extension */
typedef struct _srp_ext
{
	struct _srp_hba             *p_hba;

}   srp_ext_t;
/*
* NOTES
*   The device extension only contains a pointer to our dynamically
*   allocated HBA structure.  This is done since we don't have control
*   over the destruction of the device extension, but we need to be able to
*   control the creation and destruction of the HBA object.  We hook the driver
*   unload routine so that we can clean up any remaining objects.
*********/

extern BOOLEAN     g_srp_system_shutdown;


#endif  /* _SRP_DATA_H_ */
