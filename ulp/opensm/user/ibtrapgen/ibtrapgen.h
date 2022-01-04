/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  This software program is available to you under a choice of one of two
  licenses.  You may choose to be licensed under either the GNU General Public
  License (GPL) Version 2, June 1991, available at
  http://www.fsf.org/copyleft/gpl.html, or the Intel BSD + Patent License,
  the text of which follows:

  "Recipient" has requested a license and Intel Corporation ("Intel")
  is willing to grant a license for the software entitled
  InfiniBand(tm) System Software (the "Software") being provided by
  Intel Corporation.

  The following definitions apply to this License:

  "Licensed Patents" means patent claims licensable by Intel Corporation which
  are necessarily infringed by the use or sale of the Software alone or when
  combined with the operating system referred to below.

  "Recipient" means the party to whom Intel delivers this Software.
  "Licensee" means Recipient and those third parties that receive a license to
  any operating system available under the GNU Public License version 2.0 or
  later.

  Copyright (c) 1996-2003 Intel Corporation. All rights reserved.

  The license is provided to Recipient and Recipient's Licensees under the
  following terms.

  Redistribution and use in source and binary forms of the Software, with or
  without modification, are permitted provided that the following
  conditions are met:
  Redistributions of source code of the Software may retain the above copyright
  notice, this list of conditions and the following disclaimer.

  Redistributions in binary form of the Software may reproduce the above
  copyright notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  Neither the name of Intel Corporation nor the names of its contributors shall
  be used to endorse or promote products derived from this Software without
  specific prior written permission.

  Intel hereby grants Recipient and Licensees a non-exclusive, worldwide,
  royalty-free patent license under Licensed Patents to make, use, sell, offer
  to sell, import and otherwise transfer the Software, if any, in source code
  and object code form. This license shall include changes to the Software that
  are error corrections or other minor changes to the Software that do not add
  functionality or features when the Software is incorporated in any version of
  a operating system that has been distributed under the GNU General Public
  License 2.0 or later.  This patent license shall apply to the combination of
  the Software and any operating system licensed under the GNU Public License
  version 2.0 or later if, at the time Intel provides the Software to
  Recipient, such addition of the Software to the then publicly
  available versions of such operating system available under the GNU
  Public License version 2.0 or later (whether in gold, beta or alpha
  form) causes such combination to be covered by the Licensed
  Patents. The patent license shall not apply to any other
  combinations which include the Software. No hardware per se is
  licensed hereunder.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS CONTRIBUTORS
  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
  OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
  --------------------------------------------------------------------------*/


/*
 * Abstract:
 *    Declaration of ibtrapgen_t.
 * This object represents the ibtrapgen object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.1 $
 */

#ifndef _IBTRAPGEN_H_
#define _IBTRAPGEN_H_

#include <complib/cl_qmap.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_sa_api.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>

/****h* Trap_Generator_App/Ibtrapgen
 * NAME
 * Ibtrapgen
 *
 * DESCRIPTION
 * The Ibtrapgen object create/join and leave multicast group.
 *
 * AUTHOR
 * Yael Kalka, Mellanox.
 *
 *********/

/****s* Trap_Generator_App/ibtrapgen_opt_t
 * NAME
 * ibtrapgen_opt_t
 *
 * DESCRIPTION
 * Ibtrapgen options structure.  This structure contains the various
 * specific configuration parameters for ibtrapgen.
 *
 * SYNOPSYS
 */
typedef struct _ibtrapgen_opt
{
  uint8_t   trap_num;
  uint16_t  number;
  uint16_t  rate;
  uint16_t  lid;
  uint16_t  sm_lid;
  uint8_t  src_port;
  uint8_t  port_num;
  uint32_t  transaction_timeout;
  boolean_t force_log_flush;
  char *log_file;
} ibtrapgen_opt_t;
/*
 * FIELDS
 *
 * trap_num
 *    Trap number to generate.
 *
 * number
 *    Number of times trap should be generated.
 *
 * rate
 *    Rate of trap generation (in miliseconds)
 *
 * lid
 *    Lid from which the trap should be generated.
 *
 * src_port
 *   Source port from which the trap should be generated.
 *
 * port_num
 *   Port num used for communicating with the SA.
 * 
 * SEE ALSO
 *********/

/****s* Trap Generator App/ibtrapgen_t
 * NAME
 * ibtrapgen_t
 *
 * DESCRIPTION
 * Ibtrapgen structure.
 *
 * This object should be treated as opaque and should
 * be manipulated only through the provided functions.
 *
 * SYNOPSYS
 */
typedef struct _ibtrapgen
{
  osm_log_t          *p_log;
  struct _osm_vendor *p_vendor;
  osm_bind_handle_t   h_bind;
  osm_mad_pool_t      mad_pool;

  ibtrapgen_opt_t    *p_opt;
  ib_net64_t          port_guid;
} ibtrapgen_t;
/*
 * FIELDS
 * p_log
 *    Log facility used by all Ibtrapgen components.
 *
 * p_vendor
 *    Pointer to the vendor transport layer.
 *
 *  h_bind
 *     The bind handle obtained by osm_vendor_sa_api/osmv_bind_sa
 *
 *  mad_pool
 *     The mad pool provided for teh vendor layer to allocate mad wrappers in
 *
 * p_opt
 *    ibtrapgen options structure
 *
 * guid
 *    guid for the port over which ibtrapgen is running.
 *
 * SEE ALSO
 *********/

/****f* Trap_Generator_App/ibtrapgen_destroy
 * NAME
 * ibtrapgen_destroy
 *
 * DESCRIPTION
 * The ibtrapgen_destroy function destroys an ibtrapgen object, releasing
 * all resources.
 *
 * SYNOPSIS
 */
void ibtrapgen_destroy( IN ibtrapgen_t * p_ibtrapgen );

/*
 * PARAMETERS
 * p_ibtrapgen
 *    [in] Pointer to a Trap_Generator_App object to destroy.
 *
 * RETURN VALUE
 * This function does not return a value.
 *
 * NOTES
 * Performs any necessary cleanup of the specified Trap_Generator_App object.
 * Further operations should not be attempted on the destroyed object.
 * This function should only be called after a call to ibtrapgen_init.
 *
 * SEE ALSO
 * ibtrapgen_init
 *********/

/****f* Trap_Generator_App/ibtrapgen_init
 * NAME
 * ibtrapgen_init
 *
 * DESCRIPTION
 * The ibtrapgen_init function initializes a Trap_Generator_App object for use.
 *
 * SYNOPSIS
 */
ib_api_status_t ibtrapgen_init( IN ibtrapgen_t * const p_ibtrapgen,
                              IN ibtrapgen_opt_t * const p_opt,
                              IN const osm_log_level_t log_flags
                              );

/*
 * PARAMETERS
 * p_ibtrapgen
 *    [in] Pointer to an ibtrapgen_t object to initialize.
 *
 * p_opt
 *    [in] Pointer to the options structure.
 *
 * log_flags
 *    [in] Log level flags to set.
 *
 * RETURN VALUES
 * IB_SUCCESS if the Trap_Generator_App object was initialized successfully.
 *
 * NOTES
 * Allows calling other Trap_Generator_App methods.
 *
 * SEE ALSO
 * ibtrapgen object, ibtrapgen_construct, ibtrapgen_destroy
 *********/


/****f* Trap_Generator_App/ibtrapgen_bind
 * NAME
 * ibtrapgen_bind
 *
 * DESCRIPTION
 * Binds ibtrapgen to a local port.
 *
 * SYNOPSIS
 */
ib_api_status_t ibtrapgen_bind( IN ibtrapgen_t * p_ibtrapgen );
/*
 * PARAMETERS
 * p_ibtrapgen
 *    [in] Pointer to an ibtrapgen_t object.
 *
 * RETURN VALUES
 * IB_SUCCESS if OK
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* Trap_Generator_App/ibtrapgen_run
 * NAME
 * ibtrapgen_run
 *
 * DESCRIPTION
 * Runs the ibtrapgen flow: Creation of traps.
 *
 * SYNOPSIS
 */
ib_api_status_t ibtrapgen_run( IN ibtrapgen_t * const p_ibtrapgen );

/*
 * PARAMETERS
 * p_ibtrapgen
 *    [in] Pointer to an ibtrapgen_t object.
 *
 * RETURN VALUES
 * IB_SUCCESS on success
 *
 * NOTES
 *
 * SEE ALSO
 *********/

#endif /* */
