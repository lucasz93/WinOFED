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
 *    Command line interface for ibtrapgen.
 *    Parse and fill in the options and call the actual code.
 *    Implemented in ibtrapgen:
 *     Initialize the ibmgrp object (and log) 
 *     Bind ibmgrp to the requested IB port.
 *     Run the actual command
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.1 $
 */

#include <stdio.h>
#include <stdlib.h>
#ifndef __WIN__
#include <getopt.h>
#endif
#include <complib/cl_debug.h>
#include <errno.h>
#include "ibtrapgen.h"

#define DEFAULT_RETRY_COUNT 3
#define DEFAULT_TRANS_TIMEOUT_MILLISEC 1000

/**********************************************************************
 **********************************************************************/
boolean_t
ibtrapgen_is_debug()
{
#if defined( _DEBUG_ )
  return TRUE;
#else
  return FALSE;
#endif /* defined( _DEBUG_ ) */
}

/**********************************************************************
 **********************************************************************/
void show_usage(void);

void
show_usage(  )
{
  printf( "\n------- ibtrapgen - Usage and options ----------------------\n" );
  printf( "Usage: one of the following optional flows:\n" );
  printf(" ibtrapgen -t|--trap_num <TRAP_NUM> -n|--number <NUM_TRAP_CREATIONS>\n" 
         "           -r|--rate <TRAP_RATE> -l|--lid <LIDADDR> \n"
         "           -s|--src_port <SOURCE_PORT> -p|--port_num <PORT_NUM>\n" );
  printf( "\nOptions:\n" );
  printf( "-t <TRAP_NUM>\n"
          "--trap_num <TRAP_NUM>\n"
          "          This option specifies the number of the trap to generate.\n"
          "          Valid values are 128-131.\n" );
  printf( "-n <NUM_TRAP_CREATIONS>\n"
          "--number <NUM_TRAP_CREATIONS>\n"
          "          This option specifies the number of times to generate this trap.\n"
          "          If not specified - default to 1.\n" );
  printf( "-r <TRAP_RATE>\n"
          "--rate <TRAP_RATE>\n"
          "          This option specifies the rate of the trap generation.\n"
          "          What is the time period between one generation and another?\n"
          "          The value is given in miliseconds. \n"
          "          If the number of trap creations is 1 - this value is ignored.\n" );
  printf( "-l <LIDADDR>\n"
          "--lid <LIDADDR>\n"
          "          This option specifies the lid address from where the trap should\n"
          "          be generated.\n" );
  printf( "-s <SOURCE_PORT>\n"
          "--src_port <SOURCE_PORT>\n"
          "          This option specifies the port number from which the trap should\n"
          "          be generated. If trap number is 128 - this value is ignored (since\n"
          "          trap 128 is not sent with a specific port number)\n" );
  printf( "-p <port num>\n"
          "--port_num <port num>\n"
          "          This is the port number used for communicating with\n"
          "          the SA.\n" );
  printf( "-h\n"
          "--help\n" "          Display this usage info then exit.\n\n" );
  printf( "-o\n"
          "--out_log_file\n"
          "          This option defines the log to be the given file.\n"
          "          By default the log goes to stdout.\n\n");
  printf( "-v\n"
          "          This option increases the log verbosity level.\n"
          "          The -v option may be specified multiple times\n"
          "          to further increase the verbosity level.\n"
          "          See the -vf option for more information about.\n"
          "          log verbosity.\n\n" );
  printf( "-V\n"
          "          This option sets the maximum verbosity level and\n"
          "          forces log flushing.\n"
          "          The -V is equivalent to '-vf 0xFF -d 2'.\n"
          "          See the -vf option for more information about.\n"
          "          log verbosity.\n\n" );
  printf( "-x <flags>\n"
          "          This option sets the log verbosity level.\n"
          "          A flags field must follow the -vf option.\n"
          "          A bit set/clear in the flags enables/disables a\n"
          "          specific log level as follows:\n"
          "          BIT    LOG LEVEL ENABLED\n"
          "          ----   -----------------\n"
          "          0x01 - ERROR (error messages)\n"
          "          0x02 - INFO (basic messages, low volume)\n"
          "          0x04 - VERBOSE (interesting stuff, moderate volume)\n"
          "          0x08 - DEBUG (diagnostic, high volume)\n"
          "          0x10 - FUNCS (function entry/exit, very high volume)\n"
          "          0x20 - FRAMES (dumps all SMP and GMP frames)\n"
          "          0x40 - currently unused.\n"
          "          0x80 - currently unused.\n"
          "          Without -x, ibtrapgen defaults to ERROR + INFO (0x3).\n"
          "          Specifying -x 0 disables all messages.\n"
          "          Specifying -x 0xFF enables all messages (see -V).\n\n" );
}

/**********************************************************************
 **********************************************************************/
/*
  Converts a GID string of the format 0xPPPPPPPPPPPPPPPP:GGGGGGGGGGGGGGGG 
  to a gid type
*/
int
str2gid(
  IN char *str,
  OUT ib_gid_t *p_gid
  );

int
str2gid( 
  IN char *str,
  OUT ib_gid_t *p_gid
  ) 
{
  ib_gid_t temp;
  char buf[38];
  char *p_prefix, *p_guid;

  CL_ASSERT(p_gid);

  strcpy(buf, str);
  p_prefix = buf;
  /*p_guid = index(buf, ':');*/
  p_guid = strchr( buf, ':' );

  if (! p_guid)
  {
    printf("Wrong format for gid %s\n", buf);
    return 1;
  }
  
  *p_guid = '\0';
  p_guid++;

  errno = 0;
  temp.unicast.prefix = cl_hton64(strtoull(p_prefix, NULL, 0));
  if (errno) {
    printf("Wrong format for gid prefix:%s (got %u)\n", 
           p_prefix, errno);
    return 1;
  }

  temp.unicast.interface_id = cl_hton64(strtoull(p_guid, NULL, 16));
  if (errno) {
    printf("Wrong format for gid guid:%s\n", p_guid);
    return 1;
  }
  
  *p_gid = temp;
  return 0;
}
void OsmReportState(IN const char *p_str)
{
}
/**********************************************************************
 **********************************************************************/
int OSM_CDECL
main( int argc,
      char *argv[] )
{
  static ibtrapgen_t ibtrapgen;
  ibtrapgen_opt_t opt = { 0 };
  ib_api_status_t status;
  uint32_t log_flags = OSM_LOG_ERROR | OSM_LOG_INFO;
  uint32_t next_option;
  const char *const short_option = "t:n:r:s:l:p:o:vVh";

  /*
   * In the array below, the 2nd parameter specified the number
   * of arguments as follows:
   * 0: no arguments
   * 1: argument
   * 2: optional
   */
  const struct option long_option[] = {
    {"trap_num",  1, NULL, 't'},
    {"number",    1, NULL, 'n'},
    {"rate",      1, NULL, 'r'},
    {"lid",       1, NULL, 'l'},
    {"src_port",  1, NULL, 's'},
    {"port_num",  1, NULL, 'p'},
    {"help",      0, NULL, 'h'},
    {"verbose",   0, NULL, 'v'},
    {"out_log_file",  1, NULL, 'o'},
    {"vf",        1, NULL, 'x'},
    {"V",         0, NULL, 'V'},

    {NULL, 0, NULL, 0}     /* Required at end of array */
  };


  opt.trap_num = 0;
  opt.number = 1; /* This is the default value */
  opt.rate = 0;
  opt.lid = 0;
  opt.src_port = 0;
  opt.port_num = 0;
  opt.log_file = NULL;
  opt.force_log_flush = FALSE;
  opt.transaction_timeout = DEFAULT_TRANS_TIMEOUT_MILLISEC;

  do
  {
    next_option = getopt_long_only( argc, argv, short_option,
                                    long_option, NULL );
    
    switch ( next_option )
    {
    case 't':
      /*
       * Define the trap number
       */
      opt.trap_num = (uint8_t)atoi( optarg );
      if ((opt.trap_num < 128) || (opt.trap_num > 131))
      {
        printf( "-E- Given trap number is illegal! \n"
                "    Supportes generation of traps 128-131.\n" );
        exit(1);
      }
      printf( "-I- Trap Number = %u\n", opt.trap_num );
      break;

    case 'n':
      /*
       * Define the number of occurences
       */
      opt.number = (uint16_t)atoi( optarg );

      printf( "-I- Number Trap Occurences = %u\n", opt.number );
      break;

    case 'r':
      /*
       * Define the rate of the trap
       */
      opt.rate = (uint16_t)atoi( optarg );

      printf( "-I- Trap Rate = %u miliseconds\n", opt.rate );
      break;


    case 'l':
      /*
       * Define the source lid of the trap
       */
        opt.lid = (uint16_t)strtoul( optarg , NULL , 16);

      printf( "-I- Trap Lid = 0x%04X\n", opt.lid );
      break;
      
    case 's':
      /*
       * Define the source port number of the trap
       */
      opt.src_port = (uint8_t)atoi( optarg );
      
      printf( "-I- Trap Port Number = %u\n", opt.src_port );
      break;

    case 'p':
      /*
       * Specifies port guid with which to bind.
       */
      opt.port_num = (uint8_t)atoi( optarg );
      printf( "-I- Port Num:%u\n", opt.port_num );
      break;
 
    case 'o':
      opt.log_file = optarg;
      printf("-I- Log File:%s\n", opt.log_file );
      break;

    case 'v':
      /*
       * Increases log verbosity.
       */
      log_flags = ( log_flags << 1 ) | 1;
      printf( "-I- Verbose option -v (log flags = 0x%X)\n", log_flags );
      break;

    case 'V':
      /*
       * Specifies maximum log verbosity.
       */
      log_flags = 0xFFFFFFFF;
      opt.force_log_flush = TRUE;
      printf( "-I- Enabling maximum log verbosity\n" );
      break;

    case 'h':
      show_usage(  );
      return 0;

    case 'x':
      log_flags = strtol( optarg, NULL, 0 );
      printf( "-I- Verbose option -vf (log flags = 0x%X)\n",
              log_flags );
      break;

    case -1:
      /*      printf( "Done with args\n" ); */
      break;

    default:            /* something wrong */
      abort(  );
    }

  }
  while( next_option != -1 );

  /* Check for mandatory options */
  if (opt.trap_num == 0)
  {
    printf( "-E- Missing trap number.\n" );
    exit(1);
  }
  if (opt.lid == 0)
  {
    printf( "-E- Missing lid.\n" );
    exit(1);
  }
  if (opt.src_port == 0 && opt.trap_num >= 129 && opt.trap_num <= 131)
  {
    /* for trap 129-131 should be given source port number */
    printf( "-E- source port number.\n" );
    exit(1);
  }
  if (opt.port_num == 0)
  {
    printf( "-E- Missing port number.\n" );
    exit(1);
  }
  if (opt.rate == 0 && opt.number > 1)
  {
    /* for number of traps greater than 1 need to give the rate for the
       trap generation. */
    printf( "-E- Missing rate.\n" );
    exit(1);
  }


  /* init the main object and sub objects (log and osm vendor) */
  status = ibtrapgen_init( &ibtrapgen, &opt, ( osm_log_level_t ) log_flags );
  if( status != IB_SUCCESS )
  {
    printf("-E- fail to init ibtrapgen.\n");
    goto Exit;
  }
  
  /* bind to a specific port */
  status = ibtrapgen_bind( &ibtrapgen );
  if (status != IB_SUCCESS) exit(status);
  
  /* actual work */
  status = ibtrapgen_run( &ibtrapgen );
  if (status != IB_SUCCESS)
  {
    printf("IBTRAPGEN: FAIL\n");
  }
  else
  {
    printf("IBTRAPGEN: PASS\n");
  }
  
  //ibtrapgen_destroy( &ibtrapgen );
  
 Exit:
  exit ( status );
}
