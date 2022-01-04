/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2002 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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


/*
 * Abstract:
 * 	Command line interface for cmtest.
 *
 * Environment:
 * 	User Mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <complib/cl_atomic.h>
#include <complib/cl_debug.h>
#include <complib/cl_event.h>
#include <complib/cl_math.h>
#include <complib/cl_mutex.h>
#include <complib/cl_qlist.h>
#include <complib/cl_thread.h>
#include <complib/cl_timer.h>
#include <iba/ib_types.h>
#include <iba/ib_al.h>


/* Globals */
#define	CMT_DBG_VERBOSE		1

#define	CMT_BASE_SVC_ID		0xFFEE
#define CMT_ACCESS_CTRL		(IB_AC_LOCAL_WRITE + IB_AC_RDMA_READ + IB_AC_RDMA_WRITE)
#define BAD_PKEY_INDEX		0xFFFF


typedef enum _cmtest_state
{
	test_idle, test_connecting, test_transfering, test_disconnecting

}	cmtest_state_t;



typedef struct _ib_root
{
	ib_al_handle_t		h_al;
	ib_pd_handle_t		h_pd;

	/* Input parameters to control test. */
	int32_t				num_nodes;
	uint32_t			num_msgs;
	boolean_t			per_msg_buf;
	cl_mutex_t			mutex;

	cmtest_state_t		state;
	atomic32_t			num_connected;
	uint32_t			conn_index;		/* current connection id */
	uint32_t			total_sent;
	uint32_t			total_recv;

	uint32_t			num_iter;

	uint32_t			msg_size;

	ib_ca_handle_t		h_ca;
	ib_net16_t			l_lid;
	ib_net16_t			r_lid;
	ib_net64_t			ca_guid;
	ib_net64_t			port_guid;
	uint8_t				port_num;
	uint16_t			num_pkeys;
	ib_net16_t			*p_pkey_table;

	/* cm info */
	boolean_t			is_server;
	ib_listen_handle_t	h_listen;
	ib_path_rec_t		path_rec;

	/* CQ info. */
	boolean_t			is_polling;

	struct	_ib_node	*p_nodes;
	ib_qp_create_t		qp_create;
	ib_qp_mod_t			qp_mod_reset;
	ib_qp_mod_t			qp_mod_init;

	/* reg mem info */
	ib_mr_handle_t		h_mr;
	uint32_t			lkey;
	uint32_t			rkey;
	uint8_t				*p_mem;
	uint8_t				*p_mem_recv;
	uint8_t				*p_mem_send;

	uint64_t			conn_start_time;

	/*
	 * Connection parameters are initialized once to improve connection
	 * establishment rate.
	 */
	ib_cm_req_t			cm_req;
	ib_cm_rep_t			cm_rep;
	ib_cm_rtu_t			cm_rtu;
	ib_cm_dreq_t		cm_dreq;
	ib_cm_drep_t		cm_drep;

	uint32_t			inst_id;

}	ib_root_t;



typedef enum _cmnode_state
{
	node_idle, node_conn, node_dreq_sent, node_dreq_rcvd

}	cmnode_state_t;



typedef struct	_ib_node
{
	uint64_t			id;

	ib_cq_handle_t		h_send_cq;
	ib_cq_handle_t		h_recv_cq;
	ib_qp_handle_t		h_qp;
	uint32_t			max_inline;

	cmnode_state_t		state;
	ib_cm_handle_t		h_cm_req;
	ib_cm_handle_t		h_cm_dreq;

	uint32_t			send_cnt;
	uint32_t			recv_cnt;

}	ib_node_t;



uint32_t	cmt_dbg_lvl = 0x80000000;

ib_root_t	g_root;


static char *wc_type_text[] = {
	"IB_WC_SEND",
	"IB_WC_RDMA_WRITE",
	"IB_WC_RECV",
	"IB_WC_RDMA_READ",
	"IB_WC_MW_BIND",
	"IB_WC_FETCH_ADD",
	"IB_WC_COMPARE_SWAP",
	"IB_WC_RECV_RDMA_WRITE"
};

static char *wc_status_text[] = {
	"IB_WCS_SUCCESS",
	"IB_WCS_LOCAL_LEN_ERR",
	"IB_WCS_LOCAL_OP_ERR",
	"IB_WCS_LOCAL_EEC_OP_ERR",
	"IB_WCS_LOCAL_PROTECTION_ERR",
	"IB_WCS_WR_FLUSHED_ERR",
	"IB_WCS_MEM_WINDOW_BIND_ERR",
	"IB_WCS_REM_ACCESS_ERR",
	"IB_WCS_REM_OP_ERR",
	"IB_WCS_RNR_RETRY_ERR",
	"IB_WCS_TIMEOUT_RETRY_ERR",
	"IB_WCS_REM_INVALID_REQ_ERR",
	"IB_WCS_REM_INVALID_RD_REQ_ERR",
	"IB_WCS_INVALID_EECN",
	"IB_WCS_INVALID_EEC_STATE",
	"IB_WCS_UNMATCHED_RESPONSE",			
	"IB_WCS_CANCELED"						
};



static void AL_API
__req_cb(
	IN				ib_cm_req_rec_t				*p_cm_req_rec );

static void AL_API
__rep_cb(
	IN				ib_cm_rep_rec_t				*p_cm_rep_rec );

static void AL_API
__rtu_cb(
	IN				ib_cm_rtu_rec_t				*p_cm_rtu_rec );

static void AL_API
__rej_cb(
	IN				ib_cm_rej_rec_t				*p_cm_rej_rec );

static void AL_API
__mra_cb(
	IN				ib_cm_mra_rec_t				*p_cm_mra_rec );

static void AL_API
__apr_cb(
	IN				ib_cm_apr_rec_t				*p_cm_apr_rec );

static void AL_API
__lap_cb(
	IN				ib_cm_lap_rec_t				*p_cm_lap_rec );

static void AL_API
__dreq_cb(
	IN				ib_cm_dreq_rec_t			*p_cm_dreq_rec );

static void AL_API
__drep_cb(
	IN				ib_cm_drep_rec_t			*p_cm_drep_rec );

static boolean_t
__poll_cq(
	IN				ib_node_t					*p_node,
	IN				ib_cq_handle_t				h_cq );


/**********************************************************************
 **********************************************************************/
static void
__show_usage()
{
	printf( "\n------- cmtest - Usage and options ----------------------\n" );
	printf( "Usage:	  cmtest [options]\n");
	printf( "Options:\n" );
	printf( "-s\n"
			"--server\n"
			"          Directs cmtest to act as a Server\n" );
	printf( "-l <lid>\n"
			"--local <lid>\n"
			"          Local endpoint LID; see vstat port_lid value.\n"
                        "          LID Radix [0x Hex, 0 octal, else decimal]\n");
	printf( "-r <lid>\n"
			"--remote <lid>\n"
			"          Remote endpoint LID\n"
                        "          LID Radix [0x Hex, 0 octal, else decimal]\n" );
	printf( "-c <number>\n"
			"--connect <number>\n"
			"          Total number of connections to open.\n"
			"          Default of 1.\n" );
	printf( "-m <bytes>\n"
			"--msize <bytes>\n"
			"          Byte size of each message.\n"
			"          Default is 100 bytes.\n" );
	printf( "-n <number>\n"
			"--nmsgs <number>\n"
			"          Number of messages to send at a time.\n" );
	printf( "-p\n"
			"--permsg\n"
			"          Specify a separate buffer should be used per message.\n"
			"          Default is one buffer for all messages.\n" );
	printf( "-i <number>\n"
			"--iterate <number>\n"
			"          Set the number of times to loop through 'nmsgs'.\n"
			"          Default of 1.\n" );
	printf( "-v\n"
			"--verbose\n"
			"          Set verbosity level to debug console.\n" );
	printf( "-h\n"
			"--help\n"
			"          Display this usage info then exit.\n\n" );
}


/* Windows support. */
struct option
{
	const char		*long_name;
	unsigned long	flag;
	void			*pfn_handler;
	char			short_name;
};

static char			*optarg;

#define strtoull	strtoul


char
getopt_long(
	int					argc,
	char				*argv[],
	const char			*short_option,
	const struct option *long_option,
	void				*unused )
{
	static int i = 1;
	int j;
	char		ret = 0;

	UNUSED_PARAM( unused );

	if( i == argc )
		return -1;

	if( argv[i][0] != '-' )
		return ret;

	/* find the first character of the value. */
	for( j = 1; isalpha( argv[i][j] ); j++ )
		;
	optarg = &argv[i][j];

	if( argv[i][1] == '-' )
	{
		/* Long option. */
		for( j = 0; long_option[j].long_name; j++ )
		{
			if( strncmp( &argv[i][2], long_option[j].long_name,
				optarg - argv[i] - 2 ) )
			{
				continue;
			}

			switch( long_option[j].flag )
			{
			case 1:
				if( *optarg == '\0' )
					return 0;
			default:
				break;
			}
			ret = long_option[j].short_name;
			break;
		}
	}
	else
	{
		for( j = 0; short_option[j] != '\0'; j++ )
		{
			if( !isalpha( short_option[j] ) )
				return 0;

			if( short_option[j] == argv[i][1] )
			{
				ret = short_option[j];
				break;
			}

			if( short_option[j+1] == ':' )
			{
				if( *optarg == '\0' )
					return 0;
				j++;
			}
		}
	}
	i++;
	return ret;
}


static boolean_t
__parse_options(
	int							argc,
	char*						argv[] )
{
	uint32_t					next_option;
	const char* const			short_option = "esl:r:c:m:n:i:pvh";

	/*
		In the array below, the 2nd parameter specified the number
		of arguments as follows:
		0: no arguments
		1: argument
		2: optional
	*/
	const struct option long_option[] =
	{
		{	"event",	2,	NULL,	'e'},
		{	"server",	2,	NULL,	's'},
		{	"local",	1,	NULL,	'l'},
		{	"remote",	1,	NULL,	'r'},
		{	"connect",	1,	NULL,	'c'},
		{	"msize",	1,	NULL,	'm'},
		{	"nmsgs",	1,	NULL,	'n'},
		{	"iterate",	1,	NULL,	'i'},
		{	"permsg",	0,	NULL,	'p'},
		{	"verbose",	0,	NULL,	'v'},
		{	"help",		0,	NULL,	'h'},
		{	NULL,		0,	NULL,	 0 }	/* Required at end of array */
	};

	/* Set the default options. */
	g_root.msg_size = 100;
	g_root.num_nodes = 1;
	g_root.num_msgs = 0;
	g_root.num_iter = 1;
	g_root.is_polling = TRUE;

	/* parse cmd line arguments as input params */
	do
	{
		next_option = getopt_long( argc, argv, short_option,
			long_option, NULL );

		switch( next_option )
		{
		case 's':
			g_root.is_server = TRUE;
			printf( "\tServer mode\n" );
			break;

		case 'd':
			g_root.inst_id = strtoull( optarg, NULL, 0 );
			printf( "\tinstance_id..: %d\n", g_root.inst_id );
			break;

		case 'c':
			g_root.num_nodes = strtoull( optarg, NULL, 0 );
			printf( "\tconnections..: %d\n", g_root.num_nodes );
			break;

		case 'l':
			g_root.l_lid = (uint16_t)strtoull( optarg, NULL, 0 );
			printf( "\tlocal lid....: 0x%x\n", g_root.l_lid );
			g_root.l_lid = cl_hton16( g_root.l_lid );
			break;

		case 'r':
			g_root.r_lid = (uint16_t)strtoull( optarg, NULL, 0 );
			printf( "\tremote lid...: 0x%x\n", g_root.r_lid );
			g_root.r_lid = cl_hton16( g_root.r_lid );
			break;

		case 'm':
			g_root.msg_size = strtoull( optarg, NULL, 0 );
			printf( "\tmsg size.....: %d bytes\n", g_root.msg_size );
			break;

		case 'n':
			g_root.num_msgs = strtoull( optarg, NULL, 0 );
			printf( "\tnum msgs.....: %d\n", g_root.num_msgs );
			break;

		case 'i':
			g_root.num_iter = strtoull( optarg, NULL, 0 );
			printf( "\titerate......: %d\n", g_root.num_iter );
			break;

		case 'p':
			g_root.per_msg_buf = TRUE;
			printf( "\tper message data buffer\n" );
			break;

		case 'v':
			cmt_dbg_lvl = 0xFFFFFFFF;
			printf( "\tverbose\n" );
			break;

		case 'e':
			g_root.is_polling = FALSE;
			printf( "\tevent driven completions\n" );
			break;

		case 'h':
			__show_usage();
			return FALSE;

		case -1:
			break;

		default: /* something wrong */
			__show_usage();
			return FALSE;
		}
	} while( next_option != -1 );

	return TRUE;
}


/**********************************************************************
 **********************************************************************/
static void
__init_conn_info()
{
	/* Initialize connection request parameters. */
	g_root.cm_req.svc_id			= CMT_BASE_SVC_ID + g_root.inst_id;
	g_root.cm_req.max_cm_retries	= 5;
	g_root.cm_req.p_primary_path	= &g_root.path_rec;
	g_root.cm_req.pfn_cm_rep_cb		= __rep_cb;
	g_root.cm_req.qp_type			= IB_QPT_RELIABLE_CONN;
	g_root.cm_req.resp_res			= 3;
	g_root.cm_req.init_depth		= 1;
	g_root.cm_req.remote_resp_timeout = 20;
	g_root.cm_req.flow_ctrl			= TRUE;
	g_root.cm_req.local_resp_timeout= 20;
	g_root.cm_req.rnr_nak_timeout	= 6;
	g_root.cm_req.rnr_retry_cnt		= 3;
	g_root.cm_req.retry_cnt			= 5;
	g_root.cm_req.pfn_cm_mra_cb		= __mra_cb;
	g_root.cm_req.pfn_cm_rej_cb		= __rej_cb;

	/* Initialize connection reply parameters. */
	g_root.cm_rep.qp_type			= IB_QPT_RELIABLE_CONN;
	g_root.cm_rep.access_ctrl		= CMT_ACCESS_CTRL;
	g_root.cm_rep.sq_depth			= 0;
	g_root.cm_rep.rq_depth			= 0;
	g_root.cm_rep.init_depth		= 1;
	g_root.cm_rep.target_ack_delay	= 7;
	g_root.cm_rep.failover_accepted	= IB_FAILOVER_ACCEPT_UNSUPPORTED;
	g_root.cm_rep.flow_ctrl			= TRUE;
	g_root.cm_rep.rnr_nak_timeout	= 7;
	g_root.cm_rep.rnr_retry_cnt		= 6;
	g_root.cm_rep.pfn_cm_rej_cb		= __rej_cb;
	g_root.cm_rep.pfn_cm_mra_cb		= __mra_cb;
	g_root.cm_rep.pfn_cm_rtu_cb		= __rtu_cb;
	g_root.cm_rep.pfn_cm_lap_cb		= __lap_cb;
	g_root.cm_rep.pfn_cm_dreq_cb	= __dreq_cb;

	/* Initialize connection RTU parameters. */
	g_root.cm_rtu.pfn_cm_apr_cb		= __apr_cb;
	g_root.cm_rtu.pfn_cm_dreq_cb	= __dreq_cb;

	/* Initialize disconnection request parameters. */
	g_root.cm_dreq.pfn_cm_drep_cb	= __drep_cb;
	g_root.cm_dreq.qp_type			= IB_QPT_RELIABLE_CONN;

	/* Disconnection reply parameters are all zero. */
}



static uint16_t
__get_pkey_index()
{
	uint16_t	i;

	for( i = 0; i < g_root.num_pkeys; i++ )
	{
		if( g_root.p_pkey_table[i] == g_root.path_rec.pkey )
			return i;
	}

	return BAD_PKEY_INDEX;
}



static void
__init_qp_info()
{
	/* Set common QP attributes for all create calls. */
	g_root.qp_create.qp_type = IB_QPT_RELIABLE_CONN;
	if( g_root.num_msgs )
	{
		g_root.qp_create.sq_depth = g_root.num_msgs;
		g_root.qp_create.rq_depth = g_root.num_msgs;
	}
	else
	{
		/* Minimal queue depth of one. */
		g_root.qp_create.sq_depth = 1;
		g_root.qp_create.rq_depth = 1;
	}

	g_root.qp_create.sq_signaled = FALSE;
	g_root.qp_create.sq_sge = 1;
	g_root.qp_create.rq_sge = 1;

	/* Set the QP attributes when modifying the QP to the reset state. */
	g_root.qp_mod_reset.req_state = IB_QPS_RESET;

	/* Set the QP attributes when modifying the QP to the init state. */
	g_root.qp_mod_init.req_state = IB_QPS_INIT;
	g_root.qp_mod_init.state.init.access_ctrl = CMT_ACCESS_CTRL;
	g_root.qp_mod_init.state.init.primary_port = g_root.port_num;
	g_root.qp_mod_init.state.init.pkey_index = __get_pkey_index();
}



static ib_api_status_t
__post_recvs(
	IN				ib_node_t					*p_node )
{
	ib_api_status_t	status = IB_SUCCESS;
	ib_recv_wr_t	recv_wr;
	ib_recv_wr_t	*p_recv_failure;
	ib_local_ds_t	ds_array;
	uint32_t		i;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	if( !g_root.num_msgs )
	{
		CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
		return IB_SUCCESS;
	}

	cl_memclr( &recv_wr, sizeof( ib_recv_wr_t ) );
	ds_array.length = g_root.msg_size;
	ds_array.lkey = g_root.lkey;
	recv_wr.ds_array = &ds_array;
	recv_wr.num_ds = (( g_root.msg_size <= 4 )? 0: 1 );

	for( i = 0; i < g_root.num_msgs; i++ )
	{
		CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl, (".") );

		if( g_root.per_msg_buf )
		{
			ds_array.vaddr = (uintn_t)(g_root.p_mem_recv + (i * g_root.msg_size) +
					(p_node->id * g_root.num_msgs * g_root.msg_size));
		}
		else
		{
			ds_array.vaddr = (uintn_t)g_root.p_mem;
		}

		recv_wr.wr_id = i;
		
		status = ib_post_recv( p_node->h_qp, &recv_wr, &p_recv_failure );
		if( status != IB_SUCCESS )
		{
			printf( "ib_post_recv failed [%s]!\n", ib_get_err_str(status) );
			break;
		}
	}

	CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl, ("\n") );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return status;
}



static void AL_API
__ca_async_event_cb(
	ib_async_event_rec_t	*p_err_rec )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	
	CL_TRACE( CMT_DBG_VERBOSE, cmt_dbg_lvl, 
		( "p_err_rec->code is %d\n", p_err_rec->code ) );
	
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__cancel_listen_cb(
	IN				void						*context )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	if( !context )
		printf( "%s NULL context\n", __FUNCTION__ );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



/* We need to halt the test and recover from the reject error. */
static void AL_API
__rej_cb(
	IN				ib_cm_rej_rec_t				*p_cm_rej_rec )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	/*
	 * Note - because this callback exits the app, any output beyond the
	 * the first time may report junk.  There have been instances where
	 * the callback is invoked more times than there are connection requests
	 * but that behavior disapeared if the call to exit below is removed.
	 */
	printf( "Connection was rejected, status: 0x%x\n",
		p_cm_rej_rec->rej_status );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	exit( 1 );
}



static void AL_API
__req_cb(
	IN				ib_cm_req_rec_t				*p_cm_req_rec )
{
	ib_api_status_t	status;
	ib_node_t		*p_node;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	
	CL_ASSERT( p_cm_req_rec );

	/* Record the starting time for the server. */
	if( !g_root.conn_start_time )
		g_root.conn_start_time = cl_get_time_stamp( );

	/*
	 * Do not send replies until the server is ready to establish all
	 * connections.
	 */
	cl_mutex_acquire( &g_root.mutex );
	p_node = &g_root.p_nodes[g_root.conn_index++];

	if( g_root.state == test_connecting )
	{
		/* Get a node for this connection and send the reply. */
		g_root.cm_rep.h_qp = p_node->h_qp;
		status = ib_cm_rep( p_cm_req_rec->h_cm_req, &g_root.cm_rep );
		if( status != IB_SUCCESS )
		{
			printf( "Call to ib_cm_rep failed\n" );
			exit( 1 );
		}
	}
	else
	{
		p_node->h_cm_req = p_cm_req_rec->h_cm_req;
	}
	cl_mutex_release( &g_root.mutex );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__rep_cb(
	IN				ib_cm_rep_rec_t				*p_cm_rep_rec )
{
	ib_api_status_t	status;
	ib_node_t		*p_node;
	uint8_t			pdata[IB_RTU_PDATA_SIZE];
	ib_cm_mra_t		mra;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_rep_rec );
	
	p_node = (ib_node_t *)p_cm_rep_rec->qp_context;
	CL_ASSERT( p_node );

	mra.p_mra_pdata = NULL;
	mra.mra_length = 0;
	mra.svc_timeout = 0xff;

	ib_cm_mra( p_cm_rep_rec->h_cm_rep, &mra );

	__post_recvs( p_node );

	/* Mark that we're connected before sending the RTU. */
	p_node->state = node_conn;

	g_root.cm_rtu.p_rtu_pdata = pdata;
	g_root.cm_rtu.rtu_length = IB_RTU_PDATA_SIZE;

	status = ib_cm_rtu( p_cm_rep_rec->h_cm_rep, &g_root.cm_rtu );
	if( status != IB_SUCCESS )
	{
		printf( "Call to ib_cm_rtu returned %s\n", ib_get_err_str( status ) );
		exit( 1 );
	}

	cl_atomic_inc( &g_root.num_connected );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__rtu_cb(
	IN				ib_cm_rtu_rec_t				*p_cm_rtu_rec )
{
	ib_node_t		*p_node;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_rtu_rec );
	
	p_node = (ib_node_t*)p_cm_rtu_rec->qp_context;
	p_node->state = node_conn;

	__post_recvs( p_node );
	cl_atomic_inc( &g_root.num_connected );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__mra_cb(
	IN				ib_cm_mra_rec_t				*p_cm_mra_rec )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_mra_rec );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__apr_cb(
	IN				ib_cm_apr_rec_t				*p_cm_apr_rec )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_apr_rec );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__lap_cb(
	IN				ib_cm_lap_rec_t				*p_cm_lap_rec )
{
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_lap_rec );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__dreq_cb(
	IN				ib_cm_dreq_rec_t			*p_cm_dreq_rec )
{
	ib_node_t		*p_node;
	ib_api_status_t	status;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_dreq_rec );
	p_node = (ib_node_t*)p_cm_dreq_rec->qp_context;
	CL_ASSERT( p_node );

	/*
	 * Record that we've already received a DREQ to avoid trying to
	 * disconnect the QP a second time.  Synchronize with the DREQ call
	 * using the mutex.
	 */
	cl_mutex_acquire( &g_root.mutex );

	/* If we need to send or receive more data, don't disconnect yet. */
	if( g_root.state == test_disconnecting )
	{
		/* Send the DREP. */
		status = ib_cm_drep( p_cm_dreq_rec->h_cm_dreq, &g_root.cm_drep );

		/* If the DREP was successful, we're done with this connection. */
		if( status == IB_SUCCESS )
		{
			p_node->state = node_idle;
			cl_atomic_dec( &g_root.num_connected );
		}
	}
	else
	{
		/* Record that we need to disconnect, but don't send the DREP yet. */
		p_node->state = node_dreq_rcvd;
		p_node->h_cm_dreq = p_cm_dreq_rec->h_cm_dreq;
	}
	cl_mutex_release( &g_root.mutex );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__drep_cb(
	IN				ib_cm_drep_rec_t			*p_cm_drep_rec )
{
	ib_node_t		*p_node;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	CL_ASSERT( p_cm_drep_rec );
	p_node = (ib_node_t*)p_cm_drep_rec->qp_context;
	CL_ASSERT( p_node );

	/* We're done with this connection. */
	cl_mutex_acquire( &g_root.mutex );
	p_node->state = node_idle;
	cl_atomic_dec( &g_root.num_connected );
	cl_mutex_release( &g_root.mutex );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static void AL_API
__cq_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void*						cq_context )
{
	ib_node_t		*p_node = (ib_node_t*)cq_context;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	if( !g_root.is_polling )
	{
		if( !__poll_cq( p_node, h_cq ) )
			exit( 1 );
	}
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}


static ib_api_status_t
__create_qp(
	IN			ib_node_t			*p_node )
{
	ib_api_status_t	status;
	ib_qp_attr_t	attr;

	/* Set the per node QP attributes. */
	g_root.qp_create.h_sq_cq = p_node->h_send_cq; 
	g_root.qp_create.h_rq_cq = p_node->h_recv_cq;

	/* Allocate the QP. */
	status = ib_create_qp( g_root.h_pd, &g_root.qp_create, p_node, NULL,
		&p_node->h_qp );
	if( status != IB_SUCCESS )
	{
		printf( "[%d] ib_create_qp failed [%s]!\n", __LINE__, 
			ib_get_err_str(status) );
		return status;
	}

	/* Store the max inline size. */
	status = ib_query_qp( p_node->h_qp, &attr );
	if( status != IB_SUCCESS )
		p_node->max_inline = 0;
	else
		p_node->max_inline = attr.sq_max_inline;

	/*
	 * Transition the QP to the initialize state.  This prevents the CM
	 * from having to make this QP transition and improves the connection
	 * establishment rate.
	 */
	status = ib_modify_qp( p_node->h_qp, &g_root.qp_mod_reset );
	if( status != IB_SUCCESS )
	{
		printf( "ib_modify_qp to IB_QPS_RESET returned %s\n",
			ib_get_err_str(status) );
		return status;
	}

	status = ib_modify_qp( p_node->h_qp, &g_root.qp_mod_init );
	if( status != IB_SUCCESS )
	{
		printf( "ib_modify_qp to IB_QPS_INIT returned %s\n",
			ib_get_err_str(status) );
		return status;
	}

	return status;
}



/*
 * Allocate new QPs for all nodes.
 */
static ib_api_status_t
__create_qps()
{
	uint64_t		start_time, total_time;
	int32_t			i;
	ib_api_status_t	status = IB_SUCCESS;

	printf( "Creating QPs...\n" );
	start_time = cl_get_time_stamp();

	for( i = 0; i < g_root.num_nodes; i++ )
	{
		/* Allocate a new QP. */
		status = __create_qp( &g_root.p_nodes[i] );
		if( status != IB_SUCCESS )
			break;
	}

	total_time = cl_get_time_stamp() - start_time;
	printf( "Allocation time: %"PRId64" ms\n", total_time/1000 );

	return status;
}



/*
 * Destroy all QPs for all nodes.
 */
static void
__destroy_qps()
{
	uint64_t		start_time, total_time;
	int32_t			i;

	printf( "Destroying QPs...\n" );
	start_time = cl_get_time_stamp();

	for( i = 0; i < g_root.num_nodes; i++ )
	{
		/* Destroy the QP. */
		if( g_root.p_nodes[i].h_qp )
		{
			ib_destroy_qp( g_root.p_nodes[i].h_qp, ib_sync_destroy );
			g_root.p_nodes[i].h_qp = NULL;
		}
	}

	total_time = cl_get_time_stamp() - start_time;
	printf( "Destruction time: %"PRId64" ms\n", total_time/1000 );
}



static boolean_t
__init_node(
	IN OUT		ib_node_t*			p_node )
{
	ib_api_status_t	status;
	ib_cq_create_t	cq_create;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	/* Create the CQs. */
	cl_memclr( &cq_create, sizeof(ib_cq_create_t) );
	if( g_root.num_msgs )
		cq_create.size = g_root.num_msgs;
	else
		cq_create.size = 1;	/* minimal of one entry */

	cq_create.pfn_comp_cb = __cq_cb;
	status = ib_create_cq( g_root.h_ca, &cq_create, p_node, NULL,
		&p_node->h_send_cq );
	if( status != IB_SUCCESS )
	{
		printf( "ib_create_cq failed for send CQ [%s]!\n",
			ib_get_err_str(status) );
		return FALSE;
	}
	if( !g_root.is_polling )
	{
		status = ib_rearm_cq( p_node->h_send_cq, FALSE );
		if( status != IB_SUCCESS )
		{
			printf( "ib_rearm_cq failed for send CQ [%s]!\n",
				ib_get_err_str(status) );
			return FALSE;
		}
	}

	status = ib_create_cq( g_root.h_ca, &cq_create, p_node, NULL,
			&p_node->h_recv_cq );
	if( status != IB_SUCCESS )
	{
		printf( "ib_create_cq failed for recv CQ [%s]!\n",
			ib_get_err_str(status) );
		return FALSE;
	}
	if( !g_root.is_polling )
	{
		status = ib_rearm_cq( p_node->h_recv_cq, FALSE );
		if( status != IB_SUCCESS )
		{
			printf( "ib_rearm_cq failed for recv CQ [%s]!\n",
				ib_get_err_str(status) );
			return FALSE;
		}
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}

static boolean_t
__destroy_node(
	IN	OUT			ib_node_t*					p_node )
{
	ib_api_status_t	status = IB_SUCCESS;
	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	if (!p_node )
		return (FALSE);
	if ( p_node->h_send_cq )
	{
		status = ib_destroy_cq( p_node->h_send_cq, ib_sync_destroy );
		p_node->h_send_cq = NULL;
		if( status != IB_SUCCESS )
		{
			printf( "ib_destroy_cq failed for send CQ [%s]!\n",
				ib_get_err_str(status) );
		}
	}
	if (p_node->h_recv_cq)
	{
		status = ib_destroy_cq( p_node->h_recv_cq, ib_sync_destroy );
		p_node->h_recv_cq = NULL;
		if( status != IB_SUCCESS )
		{
			printf( "ib_destroy_cq failed for recv CQ [%s]!\n",
				ib_get_err_str(status) );
		}
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return (status == IB_SUCCESS);
}



static boolean_t
__create_nodes()
{
	int32_t			i;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	for( i = 0; i < g_root.num_nodes; i++ )
	{
		g_root.p_nodes[i].id = i;

		CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl,	
			("--> create connection %d of instance %d\n", i, g_root.inst_id) );
		
		if( !__init_node( &g_root.p_nodes[i] ) )
			return FALSE;
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}

static boolean_t
__destroy_nodes()
{
	int32_t			i;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	for( i = 0; i < g_root.num_nodes; i++ )
	{
		if( !__destroy_node( &g_root.p_nodes[i] ) )
			return FALSE;
	}
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}

/* query function called by ib_query() */
static void AL_API
__sa_query_cb(
	IN				ib_query_rec_t				*p_query_rec )
{
	ib_path_rec_t	*p_path;
	ib_api_status_t	status;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	CL_ASSERT( p_query_rec );

	if( p_query_rec->status != IB_SUCCESS )
	{
		printf( "ib_query failed [%d]\n", p_query_rec->status ); 
		return;
	}

	if( p_query_rec->query_type != IB_QUERY_PATH_REC_BY_LIDS )
	{
		printf( "Unexpected query type returned.\n" ); 
		return;
	}

	if( !p_query_rec->p_result_mad )
	{
		printf( "No result MAD returned from ib_query.\n" ); 
		return;
	}

	/* copy the 1st (zero'th) path record to local storage. */
	p_path = ib_get_query_path_rec( p_query_rec->p_result_mad, 0 );
	memcpy( (void*)&g_root.path_rec, (void*)p_path, 
		sizeof(ib_path_rec_t) );

	CL_TRACE( CMT_DBG_VERBOSE, cmt_dbg_lvl,
		( "path{ slid:0x%x, dlid:0x%x }\n", 
		g_root.path_rec.slid, g_root.path_rec.dlid) );

	/* release response MAD(s) back to AL pool */
	if( p_query_rec->p_result_mad )
	{
		status = ib_put_mad( p_query_rec->p_result_mad );
		if( status != IB_SUCCESS )
		{
			printf( "ib_put_mad() failed [%s]\n",
				ib_get_err_str(status) );
		}
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
}



static boolean_t
__query_for_path()
{
	ib_api_status_t	status;
	ib_query_req_t	query_rec;
	ib_lid_pair_t	lid_pair;

	/* Query the SA for a path record. */
	query_rec.query_type = IB_QUERY_PATH_REC_BY_LIDS;

	lid_pair.src_lid = g_root.l_lid;
	lid_pair.dest_lid = g_root.r_lid;

	query_rec.p_query_input = (void*)&lid_pair;
	query_rec.port_guid = g_root.port_guid;
	query_rec.timeout_ms = 5 * 1000;	// seconds
	query_rec.retry_cnt = 2;
	query_rec.flags = IB_FLAGS_SYNC;
	query_rec.query_context = &g_root;
	query_rec.pfn_query_cb = __sa_query_cb;

	status = ib_query( g_root.h_al, &query_rec, NULL );
 	if( ( status != IB_SUCCESS ) || ( !g_root.path_rec.dlid ) )
	{
		printf( "ib_query failed.\n" );
		return FALSE;
	}

	return TRUE;
}



static boolean_t
__create_messages()
{
	ib_mr_create_t	mr_create;
	uint32_t		buf_size;
	ib_api_status_t	status;

	/* If we're not sending messages, just return. */
	if( !g_root.num_msgs || !g_root.msg_size )
		return TRUE;

	/* Allocate the message memory - we ignore the data, so just one buffer. */
	if( g_root.per_msg_buf )
		buf_size = (g_root.num_nodes * g_root.num_msgs * g_root.msg_size) << 1;
	else
		buf_size = g_root.msg_size;
	g_root.p_mem = (uint8_t*)cl_zalloc( buf_size );
	if( !g_root.p_mem )
	{
		printf( "Not enough memory for transfers!\n" );
		return FALSE;
	}
	memset (g_root.p_mem, 0xae, buf_size);
	g_root.p_mem_recv = g_root.p_mem;
	g_root.p_mem_send = g_root.p_mem + (buf_size >> 1);

	/* Register the memory with AL. */
	mr_create.vaddr = g_root.p_mem;
	mr_create.length = buf_size;
	mr_create.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_MW_BIND;
	status = ib_reg_mem( g_root.h_pd, &mr_create, &g_root.lkey, 
		&g_root.rkey, &g_root.h_mr );
	if( status != IB_SUCCESS )
	{
		printf( "ib_reg_mem failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}

	return TRUE;
}



/*
 * PnP callback handler.  Record the port GUID of an active port.
 */
static ib_api_status_t AL_API
__pnp_cb(
	IN					ib_pnp_rec_t				*p_pnp_rec )
{
	ib_pnp_port_rec_t*	p_port_rec;
	uint32_t			size;

	p_port_rec = (ib_pnp_port_rec_t*)p_pnp_rec;

	/*
	 * Ignore PNP events that are not related to port active, or if
	 * we already have an active port.
	 */
	if( p_pnp_rec->pnp_event != IB_PNP_PORT_ACTIVE || g_root.port_guid )
		return IB_SUCCESS;

	/* Find the proper port for the given local LID. */
	if( g_root.l_lid )
	{
		if( g_root.l_lid != p_port_rec->p_port_attr->lid )
			return IB_SUCCESS;
	}
	else
	{
		g_root.l_lid = p_port_rec->p_port_attr->lid;
		printf( "\tlocal lid....: x%x\n", g_root.l_lid );
	}

	/* Record the active port information. */
	g_root.ca_guid = p_port_rec->p_ca_attr->ca_guid;
	g_root.port_num = p_port_rec->p_port_attr->port_num;

	/* Record the PKEYs available on the active port. */
	size = sizeof( ib_net16_t ) * p_port_rec->p_port_attr->num_pkeys;
	g_root.p_pkey_table = (ib_net16_t *)cl_zalloc( size );
	if( !g_root.p_pkey_table )
		return IB_SUCCESS;
	g_root.num_pkeys = p_port_rec->p_port_attr->num_pkeys;
	cl_memcpy( g_root.p_pkey_table,
		p_port_rec->p_port_attr->p_pkey_table, size );

	/* Set the port_guid last to indicate that we're ready. */
	g_root.port_guid = p_port_rec->p_port_attr->port_guid;
	return IB_SUCCESS;
}


/*
 * Register for PnP events and wait until a port becomes active.
 */
static boolean_t
__reg_pnp()
{
	ib_api_status_t		status;
	ib_pnp_req_t		pnp_req;
	ib_pnp_handle_t		h_pnp;

	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class = IB_PNP_PORT;
	pnp_req.pnp_context = &g_root;
	pnp_req.pfn_pnp_cb = __pnp_cb;

	/* Register for PnP events. */
	status = ib_reg_pnp( g_root.h_al, &pnp_req, &h_pnp );
	if( status != IB_SUCCESS )
	{
		printf( "ib_reg_pnp failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}

	/* Wait until a port goes active. */
	while( !g_root.port_guid )
		cl_thread_suspend( 10 );

	/* Deregister from PnP. */
	ib_dereg_pnp( h_pnp, NULL );

	return TRUE;
}



static boolean_t
__init_root()
{
	ib_api_status_t	status;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	cl_mutex_construct( &g_root.mutex );
	if( cl_mutex_init( &g_root.mutex ) != CL_SUCCESS )
		return FALSE;

	/* Open AL. */
	status = ib_open_al( &g_root.h_al );
	if( status != IB_SUCCESS )
	{
		printf( "ib_open_al failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}

	/* Register for PnP events, and wait until we have an active port. */
	if( !__reg_pnp() )
		return FALSE;

	/* Open the CA. */
	status = ib_open_ca( g_root.h_al,
						 g_root.ca_guid,
						 __ca_async_event_cb,
						 &g_root,
						 &g_root.h_ca );
 	if( status != IB_SUCCESS )
	{
		printf( "ib_open_ca failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}

	/* Create a PD. */
	status = ib_alloc_pd( g_root.h_ca, IB_PDT_NORMAL, &g_root, 
		&g_root.h_pd );
 	if( status != IB_SUCCESS )
	{
		printf( "ib_alloc_pd failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}

	/* Get a path record to the remote side. */
	if( !__query_for_path() )
	{
		printf( "Unable to query for path record!\n" );
		return FALSE;
	}

	/* Allocate and register memory for the messages. */
	if( !__create_messages() )
	{
		printf( "Unable to create messages!\n" );
		return FALSE;
	}

	/* Create the connection endpoints. */
	g_root.p_nodes = (ib_node_t*)cl_zalloc(
		sizeof(ib_node_t) * g_root.num_nodes );
	if( !g_root.p_nodes )
	{
		printf( "Unable to allocate nodes\n" ); 
		return FALSE;
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}



static void
__cleanup()
{
	if( g_root.h_listen )
		ib_cm_cancel( g_root.h_listen, __cancel_listen_cb );

	/* Close AL if it was opened. */
	if( g_root.h_al )
		ib_close_al( g_root.h_al );

	cl_mutex_destroy( &g_root.mutex );

	if( g_root.p_mem )
		cl_free( g_root.p_mem );

	if( g_root.p_pkey_table )
		cl_free( g_root.p_pkey_table );

	/* Free all allocated memory. */
	if( g_root.p_nodes )
		cl_free( g_root.p_nodes );
}



/*
 * Have the server start listening for connections.
 */
static boolean_t
__listen()
{
	ib_cm_listen_t	cm_listen;
	ib_api_status_t	status;

	cl_memclr( &cm_listen, sizeof( ib_cm_listen_t ) );

	/* The server side listens. */
	cm_listen.svc_id = CMT_BASE_SVC_ID + g_root.inst_id;

	cm_listen.pfn_cm_req_cb = __req_cb;

	cm_listen.qp_type = IB_QPT_RELIABLE_CONN;

	status = ib_cm_listen( g_root.h_al,
						   &cm_listen, 
						   &g_root,
						   &g_root.h_listen );
	if( status != IB_SUCCESS )
	{
		printf( "ib_cm_listen failed [%s]!\n", ib_get_err_str(status) );
		return FALSE;
	}
	return TRUE;
}



/*
 * Initiate all client connection requests.
 */
static ib_api_status_t
__conn_reqs()
{
	ib_api_status_t	status;
	int32_t			i;
	uint8_t			pdata[IB_REQ_PDATA_SIZE];

	g_root.cm_req.p_req_pdata = pdata;
	g_root.cm_req.req_length = IB_REQ_PDATA_SIZE;

	/* Request a connection for each client. */
	for( i = 0; i < g_root.num_nodes; i++ )
	{
		g_root.cm_req.h_qp = g_root.p_nodes[i].h_qp;

		status = ib_cm_req( &g_root.cm_req );
		if( status != IB_SUCCESS )
		{
			printf( "ib_cm_req failed [%s]!\n", ib_get_err_str(status) );
			return status;
		}
	}
	return IB_SUCCESS;
}



/*
 * Send any connection replies waiting to be sent.
 */
static ib_api_status_t
__conn_reps()
{
	ib_api_status_t	status;
	uintn_t			i;
	uint8_t			pdata[IB_REP_PDATA_SIZE];

	g_root.cm_rep.p_rep_pdata = pdata;
	g_root.cm_rep.rep_length = IB_REP_PDATA_SIZE;

	/* Send a reply for each connection that requires one. */
	for( i = 0; i < g_root.conn_index; i++ )
	{
		g_root.cm_rep.h_qp = g_root.p_nodes[i].h_qp;
		status = ib_cm_rep( g_root.p_nodes[i].h_cm_req, &g_root.cm_rep );
		if( status != IB_SUCCESS )
		{
			printf( "ib_cm_rep failed [%s]!\n", ib_get_err_str(status) );
			return status;
		}
	}
	return IB_SUCCESS;
}



/*
 * Establish all connections.
 */
static ib_api_status_t
__connect()
{
	uint64_t		total_time;
	ib_api_status_t	status;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	printf( "Connecting...\n" );
	cl_mutex_acquire( &g_root.mutex );
	g_root.state = test_connecting;

	/* Initiate the connections. */
	if( g_root.is_server )
	{
		/*
		 * Send any replies.  Note that we hold the mutex while sending the
		 * replies since we need to use the global cm_rep structure.
		 */
		status = __conn_reps();
		cl_mutex_release( &g_root.mutex );
	}
	else
	{
		cl_mutex_release( &g_root.mutex );
		g_root.conn_start_time = cl_get_time_stamp();
		status = __conn_reqs();
	}

	if( status != IB_SUCCESS )
		return status;

	/* Wait for all connections to complete. */
	while( g_root.num_connected < g_root.num_nodes )
		cl_thread_suspend( 0 );

	/* Calculate the total connection time. */
	total_time = cl_get_time_stamp() - g_root.conn_start_time;
	g_root.state = test_idle;

	/* Reset connection information for next test. */
	g_root.conn_index = 0;
	g_root.conn_start_time = 0;

	printf( "Connect time: %"PRId64" ms\n", total_time/1000 );

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return status;
}



static void
__disconnect()
{
	ib_api_status_t	status;
	int32_t			i;
	ib_node_t		*p_node;
	uint64_t		total_time, start_time;

	printf( "Disconnecting...\n" );

	/* Initiate the disconnection process. */
	cl_mutex_acquire( &g_root.mutex );
	g_root.state = test_disconnecting;
	start_time = cl_get_time_stamp();
	cl_mutex_release( &g_root.mutex );

	/* We hold the mutex to prevent calling ib_cm_drep at the same time. */
	for( i = 0; i < g_root.num_nodes; i++ )
	{
		p_node = &g_root.p_nodes[i];

		/*
		 * Send the DREQ.  Note that some of these may fail, since the
		 * remote side may be disconnecting at the same time.  Call DREQ
		 * only if we haven't called DREP yet.
		 */
		cl_mutex_acquire( &g_root.mutex );
		switch( p_node->state )
		{
		case node_conn:
			g_root.cm_dreq.h_qp = p_node->h_qp;
			status = ib_cm_dreq( &g_root.cm_dreq );
			if( status == IB_SUCCESS )
				p_node->state = node_dreq_sent;
			break;

		case node_dreq_rcvd:
			status = ib_cm_drep( p_node->h_cm_dreq, &g_root.cm_drep );

			/* If the DREP was successful, we're done with this connection. */
			if( status == IB_SUCCESS )
			{
				p_node->state = node_idle;
				cl_atomic_dec( &g_root.num_connected );
			}
			break;

		default:
			/* Node is already disconnected. */
			break;
		}
		cl_mutex_release( &g_root.mutex );
	}

	/* Wait for all disconnections to complete. */
	while( g_root.num_connected )
		cl_thread_suspend( 0 );

	if( g_root.h_listen )
	{
		ib_cm_cancel( g_root.h_listen, __cancel_listen_cb );
		g_root.h_listen = NULL;
	}
	/* Calculate the total connection time. */
	total_time = cl_get_time_stamp() - start_time;
	g_root.state = test_idle;

	printf( "Disconnect time: %"PRId64" ms\n", total_time/1000 );
}



/*
 * Send the requested number of messages on each connection.
 */
static boolean_t
__send_msgs()
{
	ib_api_status_t	status;
	int32_t			i;
	uint32_t		m;
	ib_send_wr_t	send_wr;
	ib_send_wr_t	*p_send_failure;
	ib_local_ds_t	ds_array;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	/* For each connection... */
	for( i = 0; i < g_root.num_nodes; i++ )
	{
		/* Send the specified number of messages. */
		for( m = 0; m < g_root.num_msgs; m++ )
		{
			/* Get the buffer for this message. */
			if( g_root.per_msg_buf )
			{
				ds_array.vaddr = (uintn_t)(g_root.p_mem_send +
					(i * g_root.num_msgs) + (m * g_root.msg_size));
			}
			else
			{
				ds_array.vaddr = (uintn_t)g_root.p_mem;
			}
			ds_array.length = g_root.msg_size;
			ds_array.lkey = g_root.lkey;

			/* Format the send WR for this message. */
			send_wr.ds_array = &ds_array;
			send_wr.send_opt = IB_SEND_OPT_SIGNALED | IB_SEND_OPT_SOLICITED;
			send_wr.send_opt |= ((g_root.msg_size <= 4)? IB_SEND_OPT_IMMEDIATE : 0x0 );
			send_wr.wr_type = WR_SEND;
			send_wr.num_ds = ((g_root.msg_size <= 4)? 0 : 1 );
			send_wr.p_next = NULL;
			send_wr.wr_id = m;

			if( g_root.msg_size < g_root.p_nodes[i].max_inline )
				send_wr.send_opt |= IB_SEND_OPT_INLINE;

			/* Torpedoes away!  Send the message. */
			CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl, (".") );
			status = ib_post_send( g_root.p_nodes[i].h_qp, &send_wr,
				&p_send_failure );
			if( status != IB_SUCCESS )
			{
				printf( "ib_post_send failed [%s]!\n",
					ib_get_err_str(status) );
				return FALSE;
			}
		}
	}
	
	CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl, ("\n") );
	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}



/*
 * Remove num_msgs completions from the specified CQ.
 */
static boolean_t
__poll_cq(
	IN				ib_node_t					*p_node,
	IN				ib_cq_handle_t				h_cq )
{
	ib_api_status_t status = IB_SUCCESS;
	ib_wc_t			free_wc[2];
	ib_wc_t			*p_free_wc, *p_done_wc;

	CL_ENTER( CMT_DBG_VERBOSE, cmt_dbg_lvl );

	while( status != IB_NOT_FOUND )
	{
		/* Get all completions. */
		p_free_wc = &free_wc[0];
		free_wc[0].p_next = &free_wc[1];
		free_wc[1].p_next = NULL;
		p_done_wc = NULL;

		status = ib_poll_cq( h_cq, &p_free_wc, &p_done_wc );

		/* Continue polling if nothing is done. */
		if( status == IB_NOT_FOUND )
			break;

		/* Abort if an error occurred. */
		if( status != IB_SUCCESS )
		{
			printf( "Error polling status = %#x( wc_status =%s)\n", 
				status,
				((p_done_wc != NULL )? wc_status_text[p_done_wc->status]:"N/A") );
			return FALSE;
		}

		while( p_done_wc )
		{
			switch( p_done_wc->status )
			{
			case IB_WCS_SUCCESS:
				CL_PRINT( CMT_DBG_VERBOSE, cmt_dbg_lvl,
					("Got a completion: \n\ttype....:%s\n\twr_id...:%"PRIx64"\n"
					"status....:%s\n", wc_type_text[p_done_wc->wc_type],
					p_done_wc->wr_id, wc_status_text[p_done_wc->status] ) );

				if( p_done_wc->wc_type == IB_WC_RECV )
				{
					CL_ASSERT( p_done_wc->wr_id == p_node->recv_cnt );
					if( p_done_wc->length != g_root.msg_size )
					{
						printf( "Error: received %d bytes, expected %d.\n",
							p_done_wc->length, g_root.msg_size );
					}

					p_node->recv_cnt++;
					g_root.total_recv++;
				}
				else
				{
					CL_ASSERT( p_done_wc->wr_id == p_node->send_cnt );
					p_node->send_cnt++;
					g_root.total_sent++;
				}
				break;

			default:
				printf( "[%d] Bad completion type(%s) status(%s)\n",
					__LINE__, wc_type_text[p_done_wc->wc_type],
					wc_status_text[p_done_wc->status] );
				return FALSE;
			}
			p_done_wc = p_done_wc->p_next;
		}
	}

	if( !g_root.is_polling )
	{
		status = ib_rearm_cq(h_cq, FALSE);
		if (status != IB_SUCCESS)
		{
			printf("Failed to rearm CQ %p\n", h_cq );
			return FALSE;
		}
	}

	CL_EXIT( CMT_DBG_VERBOSE, cmt_dbg_lvl );
	return TRUE;
}



/*
 * Remove num_msgs completions from all send CQs for all connections.
 */
static boolean_t
__poll_send_cqs()
{
	ib_node_t		*p_node;
	int32_t			i;

	for( i = 0; i < g_root.num_nodes; i++ )
	{
		p_node = &g_root.p_nodes[i];
		while( p_node->send_cnt < g_root.num_msgs )
		{
			if( !g_root.is_polling )
				cl_thread_suspend( 0 );
			else if( !__poll_cq( p_node, p_node->h_send_cq ) )
				return FALSE;
		}
	}
	return TRUE;
}



/*
 * Remove num_msgs completions from all receive CQs for all connections.
 */
static boolean_t
__poll_recv_cqs()
{
	ib_node_t		*p_node;
	int32_t			i;

	for( i = 0; i < g_root.num_nodes; i++ )
	{
		p_node = &g_root.p_nodes[i];
		while( p_node->recv_cnt < g_root.num_msgs )
		{
			if( !g_root.is_polling )
				cl_thread_suspend( 0 );
			else if( !__poll_cq( p_node, p_node->h_recv_cq ) )
				return FALSE;
		}
	}
	return TRUE;
}



/**********************************************************************
 **********************************************************************/

int __cdecl
main(
	int							argc,
	char*						argv[] )
{
	uint64_t					start_time, total_time;
	uint64_t					total_xfer;
	uint32_t					i;

	cl_memclr( &g_root, sizeof(ib_root_t) );

	/* Set defaults. */
	if( !__parse_options( argc, argv ) )
		return 1;

	/* Initialize the root - open all common HCA resources. */
	if( !__init_root() )
	{
		printf( "__init_root failed\n" );
		__cleanup();
		return 1;
	}

	/*
	 * Execute the test the specified number of times.  Abort the test
	 * if any errors occur.
	 */
	total_xfer = g_root.num_msgs * g_root.msg_size * g_root.num_nodes;
	for( i = 0; i < g_root.num_iter; i++ )
	{
		printf( "----- Iteration: %d, %d connections -----\n",
			i, g_root.num_nodes );

		/* Initialize the connection parameters. */
		__init_conn_info();
		__init_qp_info();
		__create_nodes();
		/* Start listening for connections if we're the server. */
		if( g_root.is_server )
			__listen();

		cl_thread_suspend(1000);
		/* Allocate a new set of QPs for the connections. */
		if( __create_qps() != IB_SUCCESS )
		{
			printf( "Unable to allocate QPs for test.\n" );
			break;
		}

		/* Establish all connections. */
		if( __connect() != IB_SUCCESS )
		{
			printf( "Failed to establish connections.\n" );
			break;
		}

		printf( "Transfering data...\n" );
		g_root.state = test_transfering;
		start_time = cl_get_time_stamp();

		if( g_root.num_msgs )
		{
			if( g_root.is_server )
			{
				/* The server initiate the sends to avoid race conditions. */
				if( !__send_msgs() )
					break;

				/* Get all send completions. */
				if( !__poll_send_cqs() )
					break;

				/* Get all receive completions. */
				if( !__poll_recv_cqs() )
					break;
			}
			else
			{
				/* Get all receive completions. */
				if( !__poll_recv_cqs() )
					break;

				/* Reply to the sends. */
				if( !__send_msgs() )
					break;

				/* Get all send completions. */
				if( !__poll_send_cqs() )
					break;
			}
		}

		total_time = cl_get_time_stamp() - start_time;
		g_root.state = test_idle;

		printf( "Data transfer time: %"PRId64" ms, %d messages/conn, "
			"%"PRId64" total bytes", total_time/1000,
			g_root.num_msgs, total_xfer );
		if ( total_xfer > (4*1024*1024) )
		{
			double mb;
			mb = ((double)total_xfer / (1024.0*1024.0))
				/ ((double)total_time / 1000000.0);
			printf(" %4.2f MB/s",mb);
		}
		printf("\n");

		/* Disconnect all connections. */
		__disconnect();
		__destroy_qps();
		__destroy_nodes();
	}

	__cleanup();
	return 0;
}
