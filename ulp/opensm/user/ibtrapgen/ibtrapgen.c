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
 *    Implementation of ibtrapgen_t.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.2 $
 */

#include <complib/cl_qmap.h>
#include <opensm/osm_log.h>
#include <complib/cl_debug.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_sa_api.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include "ibtrapgen.h"

#define GUID_ARRAY_SIZE 64

/**********************************************************************
 **********************************************************************/
/*
  This function initializes the main object, the log and the Osm Vendor
*/
ib_api_status_t
ibtrapgen_init( IN ibtrapgen_t * const p_ibtrapgen,
              IN ibtrapgen_opt_t * const p_opt,
              IN const osm_log_level_t log_flags
              )
{
  ib_api_status_t status;

  /* just making sure - cleanup the static global obj */
  cl_memclr( p_ibtrapgen, sizeof( *p_ibtrapgen ) );

  /* construct and init the log */
  p_ibtrapgen->p_log = (osm_log_t *)cl_malloc(sizeof(osm_log_t));
  osm_log_construct( p_ibtrapgen->p_log );
  status = osm_log_init( p_ibtrapgen->p_log, p_opt->force_log_flush,
                         0x0001, p_opt->log_file,FALSE );
  if( status != IB_SUCCESS )
    return ( status );

  osm_log_set_level( p_ibtrapgen->p_log, log_flags );

  /* finaly can declare we are here ... */
  osm_log( p_ibtrapgen->p_log, OSM_LOG_FUNCS,
           "ibtrapgen_init: [\n" );

  /* assign all the opts */
  p_ibtrapgen->p_opt = p_opt;

  /* initialize the osm vendor service object */
  p_ibtrapgen->p_vendor = osm_vendor_new( p_ibtrapgen->p_log,
                                        p_opt->transaction_timeout );

  if( p_ibtrapgen->p_vendor == NULL )
  {
    status = IB_INSUFFICIENT_RESOURCES;
    osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
             "ibtrapgen_init: ERR 0001: "
             "Unable to allocate vendor object" );
    goto Exit;
  }

  /* all mads (actually wrappers) are taken and returned to a pool */
  osm_mad_pool_construct( &p_ibtrapgen->mad_pool );
  status = osm_mad_pool_init( &p_ibtrapgen->mad_pool );
  if( status != IB_SUCCESS )
    goto Exit;

 Exit:
  osm_log( p_ibtrapgen->p_log, OSM_LOG_FUNCS,
           "ibtrapgen_init: ]\n" );
  return ( status );
}

/****f* opensm: SM/__ibtrapgen_rcv_callback
 * NAME
 * __osm_sm_mad_ctrl_rcv_callback
 *
 * DESCRIPTION
 * This is the callback from the transport layer for received MADs.
 *
 * SYNOPSIS
 */
void
__ibtrapgen_rcv_callback(
  IN osm_madw_t *p_madw,
  IN void *bind_context,
  IN osm_madw_t *p_req_madw )
{
  ibtrapgen_t* p_ibtrapgen = (ibtrapgen_t*)bind_context;

  OSM_LOG_ENTER( p_ibtrapgen->p_log );

  CL_ASSERT( p_madw );

  OSM_LOG( p_ibtrapgen->p_log, OSM_LOG_VERBOSE,
           "Got callback trans_id %64I\n",
           cl_ntoh64(p_madw->p_mad->trans_id) );

  OSM_LOG_EXIT( p_ibtrapgen->p_log );
}

/****f* opensm: SM/__ibtrapgen_send_err_cb
 * NAME
 * __ibtrapgen_send_err_cb
 *
 * DESCRIPTION
 * This is the callback from the transport layer for received MADs.
 *
 * SYNOPSIS
 */
void
__ibtrapgen_send_err_cb(
  IN void *bind_context,
  IN osm_madw_t *p_madw )
{
  ibtrapgen_t* p_ibtrapgen = (ibtrapgen_t*)bind_context;

  OSM_LOG_ENTER( p_ibtrapgen->p_log );

  osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
           "__ibtrapgen_send_err_cb: ERR 0011: "
           "MAD completed in error (%s).\n",
           ib_get_err_str( p_madw->status ) );

  CL_ASSERT( p_madw );

  osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
           "__ibtrapgen_send_err_cb: ERR 0012: "
           "We shouldn't be here!! TID:0x%016" PRIx64 ".\n",
           cl_ntoh64(p_madw->p_mad->trans_id) );
  OSM_LOG_EXIT( p_ibtrapgen->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
ibtrapgen_bind( IN ibtrapgen_t * p_ibtrapgen )
{
  ib_api_status_t status;
  uint32_t num_ports = GUID_ARRAY_SIZE;
  ib_port_attr_t attr_array[GUID_ARRAY_SIZE];
  osm_bind_info_t bind_info;
  uint8_t i;

  OSM_LOG_ENTER( p_ibtrapgen->p_log );

  /*
   * Call the transport layer for a list of local port
   * GUID values.
   */
  status = osm_vendor_get_all_port_attr( p_ibtrapgen->p_vendor,
                                         attr_array, &num_ports );
  if ( status != IB_SUCCESS )
  {
    osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
             "ibtrapgen_bind: ERR 0002: "
             "Failure getting local port attributes (%s)\n",
             ib_get_err_str( status ) );
    goto Exit;
  }

  /* make sure the requested port exists */
  if ( p_ibtrapgen->p_opt->port_num > num_ports )
  {
    osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
             "ibtrapgen_bind: ERR 0003: "
             "Given port number out of range %u > %u\n",
             p_ibtrapgen->p_opt->port_num , num_ports );
    status = IB_NOT_FOUND;
    goto Exit;
  }

  for ( i = 0 ; i < num_ports ; i++ )
  {
    osm_log(p_ibtrapgen->p_log, OSM_LOG_INFO,
            "ibtrapgen_bind: Found port number:%u "
            " with GUID:0x%016" PRIx64 "\n",
            i, cl_ntoh64(attr_array[i].port_guid) );
  }
  /* check if the port is active */
/*   if (attr_array[p_ibtrapgen->p_opt->port_num - 1].link_state < 4) */
/*   { */
/*     osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR, */
/*              "ibtrapgen_bind: ERR 0004: " */
/*              "Given port number link state is not active: %s.\n", */
/*              ib_get_port_state_str( */
/*                attr_array[p_ibtrapgen->p_opt->port_num - 1].link_state ) */
/*              ); */
/*     status = IB_NOT_FOUND; */
/*     goto Exit; */
/*   } */

  p_ibtrapgen->port_guid = attr_array[p_ibtrapgen->p_opt->port_num - 1].port_guid;
 /* save sm_lid as we need it when sending the Trap (dest lid)*/
  p_ibtrapgen->p_opt->sm_lid = attr_array[p_ibtrapgen->p_opt->port_num - 1].sm_lid;

  osm_log(p_ibtrapgen->p_log, OSM_LOG_DEBUG,
          "ibtrapgen_bind: Port Num:%u "
          "GUID:0x%016"PRIx64"\n",
          p_ibtrapgen->p_opt->port_num,
          p_ibtrapgen->port_guid );

  /* ok finaly bind the sa interface to this port */
  /* TODO - BIND LIKE THE osm_sm_mad_ctrl does */
  bind_info.class_version = 1;
  bind_info.is_report_processor = TRUE;
  bind_info.is_responder = TRUE;
  bind_info.is_trap_processor = TRUE;
  bind_info.mad_class = IB_MCLASS_SUBN_LID;
  bind_info.port_guid = p_ibtrapgen->port_guid;
  bind_info.recv_q_size = OSM_SM_DEFAULT_QP0_RCV_SIZE;
  bind_info.send_q_size = OSM_SM_DEFAULT_QP0_SEND_SIZE;

  osm_log(p_ibtrapgen->p_log, OSM_LOG_DEBUG,
          "ibtrapgen_bind: Trying to bind to GUID:0x%016"PRIx64"\n",
          bind_info.port_guid );
 
  p_ibtrapgen->h_bind = osm_vendor_bind( p_ibtrapgen->p_vendor,
                                    &bind_info,
                                    &p_ibtrapgen->mad_pool,
                                    __ibtrapgen_rcv_callback,
                                    __ibtrapgen_send_err_cb,
                                    p_ibtrapgen );

  if(  p_ibtrapgen->h_bind == OSM_BIND_INVALID_HANDLE )
  {
    osm_log( p_ibtrapgen->p_log, OSM_LOG_ERROR,
             "ibtrapgen_bind: ERR 0005: "     
             "Unable to bind to SA\n" );
    status = IB_ERROR;
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_ibtrapgen->p_log );
  return ( status );
}

/**********************************************************************
 **********************************************************************/
void
ibtrapgen_destroy( IN ibtrapgen_t * p_ibtrapgen )
{
  if( p_ibtrapgen->p_vendor )
  {
    osm_vendor_delete( &p_ibtrapgen->p_vendor );
  }

  osm_log_destroy( p_ibtrapgen->p_log );
  cl_free( p_ibtrapgen->p_log );
}

/**********************************************************************
 **********************************************************************/

ib_api_status_t
ibtrapgen_run( IN ibtrapgen_t * const p_ibtrapgen )
{
  osm_madw_t*    p_report_madw;
  ib_mad_notice_attr_t*   p_report_ntc;
  ib_mad_t*               p_mad;
  ib_smp_t*               p_smp_mad;
  osm_mad_addr_t          mad_addr;
  static atomic32_t       trap_fwd_trans_id = 0x02DAB000; 
  ib_api_status_t         status;
  osm_log_t *p_log =      p_ibtrapgen->p_log;
  uint16_t                i;

  OSM_LOG_ENTER( p_log );

  osm_log( p_log, OSM_LOG_INFO,
           "ibtrapgen_run: "
           "Sending trap:%u from LID:0x%X %u times\n",
           p_ibtrapgen->p_opt->trap_num,
           p_ibtrapgen->p_opt->lid,
           p_ibtrapgen->p_opt->number );
  
  printf("-V- SM lid is : 0x%04X\n",cl_ntoh16(p_ibtrapgen->p_opt->sm_lid));
  mad_addr.dest_lid = (p_ibtrapgen->p_opt->sm_lid);
  /* ??? - what is path_bits? What should be the value here?? */
  mad_addr.path_bits = 0;
  /* ??? - what is static_rate? What should be the value here?? */
  mad_addr.static_rate = 0;
  
  mad_addr.addr_type.smi.source_lid = cl_hton16(p_ibtrapgen->p_opt->lid);
  mad_addr.addr_type.smi.port_num = p_ibtrapgen->p_opt->src_port;
  
  for (i = 1 ; i <= p_ibtrapgen->p_opt->number ; i++ )
  {
    p_report_madw = osm_mad_pool_get( &p_ibtrapgen->mad_pool,
                                      p_ibtrapgen->h_bind,
                                      MAD_BLOCK_SIZE,
                                      &mad_addr );
    
    if( !p_report_madw )
    {
      osm_log(p_log, OSM_LOG_ERROR,
              "ibtrapgen_run: ERR 00020: "
              "osm_mad_pool_get failed.\n" );
      status = IB_ERROR;
      goto Exit;
    }
                                    
    p_report_madw->resp_expected = FALSE;

    /* advance trap trans id (cant simply ++ on some systems inside ntoh) */
    p_mad = osm_madw_get_mad_ptr( p_report_madw );
    ib_mad_init_new(p_mad,
                    IB_MCLASS_SUBN_LID,
                    1,
                    IB_MAD_METHOD_TRAP,
                    cl_hton64( (uint64_t)cl_atomic_inc( &trap_fwd_trans_id ) ),
                    IB_MAD_ATTR_NOTICE,
                    0);
    
    p_smp_mad = osm_madw_get_smp_ptr( p_report_madw );
    
    /* The payload is analyzed as mad notice attribute */
    p_report_ntc = (ib_mad_notice_attr_t*)(ib_smp_get_payload_ptr(p_smp_mad));
    
    cl_memclr( p_report_ntc, sizeof(*p_report_ntc) );
    p_report_ntc->generic_type = 0x83; /* is generic subn mgt type */
    ib_notice_set_prod_type(p_report_ntc, 2); /* A switch generator */
    p_report_ntc->g_or_v.generic.trap_num = cl_hton16(p_ibtrapgen->p_opt->trap_num);
    p_report_ntc->issuer_lid = cl_hton16(p_ibtrapgen->p_opt->lid);
    if (p_ibtrapgen->p_opt->trap_num == 128)
    {
      p_report_ntc->data_details.ntc_128.sw_lid = cl_hton16(p_ibtrapgen->p_opt->lid);
    }
    else 
    {
      p_report_ntc->data_details.ntc_129_131.lid = 
        cl_hton16(p_ibtrapgen->p_opt->lid);
      p_report_ntc->data_details.ntc_129_131.port_num = 
        p_ibtrapgen->p_opt->src_port;
    }

    status = osm_vendor_send(p_report_madw->h_bind, p_report_madw, FALSE );
    if (status != IB_SUCCESS)
    {
      osm_log(p_log, OSM_LOG_ERROR,
              "ibtrapgen_run: ERR 0021: "
              "osm_vendor_send. status = %s\n",
              ib_get_err_str(status));
      goto Exit;
    }
    osm_log(p_log, OSM_LOG_INFO,
            "ibtrapgen_run: "
            "Sent trap number:%u out of:%u\n",
            i,
            p_ibtrapgen->p_opt->number );
    /* sleep according to rate time. The usleep is in usec - need to revert
       the milisecs to usecs. */
    usleep(p_ibtrapgen->p_opt->rate*1000);
  }

 Exit:
  OSM_LOG_EXIT( p_log );
  return(status);
}
