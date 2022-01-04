/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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
 * 	Test limits for:
 *		- memory registration
 *		- CQ creation
 *		- CQ resize
 *		- QP creation
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


uint32_t	cmt_dbg_lvl = 0x80000000;


/**********************************************************************
 **********************************************************************/
static void
__show_usage()
{
	printf( "\n------- ib_limits - Usage and options ----------------------\n" );
	printf( "Usage:	  ib_limits [options]\n");
	printf( "Options:\n" );
	printf( "-m\n"
			"--memory\n"
			"\tThis option directs ib_limits to test memory registration\n" );
	printf( "-c\n"
			"--cq\n"
			"\tThis option directs ib_limits to test CQ creation\n" );
	printf( "-r\n"
			"--resize_cq\n"
			"\tThis option directs ib_limits to test CQ resize\n" );
	printf( "-q\n"
			"--qp\n"
			"\tThis option directs ib_limits to test QP creation\n" );
	printf( "-v\n"
			"--verbose\n"
			"          This option enables verbosity level to debug console.\n" );
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


boolean_t	test_mr, test_cq, test_resize, test_qp;


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
	const char* const			short_option = "mcrq:vh";

	/*
		In the array below, the 2nd parameter specified the number
		of arguments as follows:
		0: no arguments
		1: argument
		2: optional
	*/
	const struct option long_option[] =
	{
		{	"memory",	2,	NULL,	'm'},
		{	"cq",		2,	NULL,	'c'},
		{	"resize_cq",2,	NULL,	'r'},
		{	"qp",		2,	NULL,	'q'},
		{	"verbose",	0,	NULL,	'v'},
		{	"help",		0,	NULL,	'h'},
		{	NULL,		0,	NULL,	 0 }	/* Required at end of array */
	};

	test_mr = FALSE;
	test_cq = FALSE;
	test_resize = FALSE;
	test_qp = FALSE;

	/* parse cmd line arguments as input params */
	do
	{
		next_option = getopt_long( argc, argv, short_option,
			long_option, NULL );

		switch( next_option )
		{
		case 'm':
			test_mr = TRUE;
			printf( "\tTest Memory Registration\n" );
			break;

		case 'c':
			test_cq = TRUE;
			printf( "\tTest CQ\n" );
			break;

		case 'r':
			test_resize = TRUE;
			printf( "\tTest CQ Resize\n" );
			break;

		case 'q':
			test_qp = TRUE;
			printf( "\tTest QP\n" );
			break;

		case 'v':
			cmt_dbg_lvl = 0xFFFFFFFF;
			printf( "\tverbose\n" );
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


struct __mr_buf
{
	cl_list_item_t	list_item;
	ib_mr_handle_t	h_mr;
	char			buf[8192 - sizeof(ib_mr_handle_t) - sizeof(cl_list_item_t)];
};

static void
__test_mr(
	ib_pd_handle_t				h_pd )
{
	ib_api_status_t		status = IB_SUCCESS;
	struct __mr_buf		*p_mr;
	int					i = 0;
	ib_mr_create_t		mr_create;
	cl_qlist_t			mr_list;
	net32_t				lkey, rkey;
	int64_t				reg_time, dereg_time, tmp_time, cnt;

	printf( "MR testing [\n" );

	cl_qlist_init( &mr_list );
	reg_time = 0;
	dereg_time = 0;
	cnt = 0;

	do
	{
		p_mr = cl_malloc( sizeof(struct __mr_buf) );
		if( !p_mr )
		{
			i++;
			printf( "Failed to allocate memory.\n" );
			continue;
		}

		mr_create.vaddr = p_mr->buf;
		mr_create.length = sizeof(p_mr->buf);
		mr_create.access_ctrl =
			IB_AC_LOCAL_WRITE | IB_AC_RDMA_READ | IB_AC_RDMA_WRITE;

		tmp_time = cl_get_time_stamp();
		status = ib_reg_mem( h_pd, &mr_create, &lkey, &rkey, &p_mr->h_mr );
		if( status != IB_SUCCESS )
		{
			i++;
			printf( "ib_reg_mem returned %s\n", ib_get_err_str( status ) );
			cl_free( p_mr );
			continue;
		}
		reg_time += cl_get_time_stamp() - tmp_time;
		cnt++;

		cl_qlist_insert_tail( &mr_list, &p_mr->list_item );

	}	while( status == IB_SUCCESS || i < 1000 );

	while( cl_qlist_count( &mr_list ) )
	{
		p_mr = PARENT_STRUCT( cl_qlist_remove_head( &mr_list ),
			struct __mr_buf, list_item );

		tmp_time = cl_get_time_stamp();
		status = ib_dereg_mr( p_mr->h_mr );
		if( status != IB_SUCCESS )
			printf( "ib_dereg_mr returned %s\n", ib_get_err_str( status ) );
		dereg_time += cl_get_time_stamp() - tmp_time;

		cl_free( p_mr );
	}

	printf( "reg time %f, dereg time %f\n", (double)reg_time/(double)cnt,
		(double)dereg_time/(double)cnt );
	printf( "MR testing ]\n" );
}


struct __cq
{
	cl_list_item_t		list_item;
	ib_cq_handle_t		h_cq;
};

static void
__test_cq(
	ib_ca_handle_t				h_ca,
	boolean_t					resize )
{
	ib_api_status_t		status = IB_SUCCESS;
	struct __cq			*p_cq;
	int					i = 0, j;
	ib_cq_create_t		cq_create;
	cl_qlist_t			cq_list;
	cl_waitobj_handle_t	h_waitobj;
	uint32_t			size;

	printf( "CQ %stesting [\n", resize?"resize ":"" );

	cl_qlist_init( &cq_list );

	if( cl_waitobj_create( FALSE, &h_waitobj ) != CL_SUCCESS )
	{
		printf( "Failed to allocate CQ wait object.\n" );
		return;
	}

	do
	{
		p_cq = cl_malloc( sizeof(*p_cq) );
		if( !p_cq )
		{
			i++;
			printf( "Failed to allocate memory.\n" );
			continue;
		}

		cq_create.h_wait_obj = h_waitobj;
		cq_create.pfn_comp_cb = NULL;
		if( resize )
			cq_create.size = 32;
		else
			cq_create.size = 4096;

		status = ib_create_cq( h_ca, &cq_create, NULL, NULL, &p_cq->h_cq );
		if( status != IB_SUCCESS )
		{
			i++;
			printf( "ib_create_cq returned %s\n", ib_get_err_str( status ) );
			cl_free( p_cq );
			continue;
		}

		if( resize )
		{
			size = 256;
			j = 0;

			do
			{
				status = ib_modify_cq( p_cq->h_cq, &size );
				if( status == IB_SUCCESS )
				{
					size += 256;
				}
				else
				{
					j++;
					printf( "ib_modify_cq returned %s\n",
						ib_get_err_str( status ) );
				}

			} while( status == IB_SUCCESS || j < 100 );
		}

		cl_qlist_insert_tail( &cq_list, &p_cq->list_item );

	}	while( status == IB_SUCCESS || i < 1000 );

	while( cl_qlist_count( &cq_list ) )
	{
		p_cq = PARENT_STRUCT( cl_qlist_remove_head( &cq_list ),
			struct __cq, list_item );

		status = ib_destroy_cq( p_cq->h_cq, NULL );
		if( status != IB_SUCCESS )
			printf( "ib_destroy_cq returned %s\n", ib_get_err_str( status ) );

		cl_free( p_cq );
	}

	printf( "CQ %stesting ]\n", resize?"resize ":"" );
}

/**********************************************************************
 **********************************************************************/
int __cdecl
main(
	int							argc,
	char*						argv[] )
{
	ib_api_status_t		status;
	ib_al_handle_t		h_al;
	ib_ca_handle_t		h_ca;
	ib_pd_handle_t		h_pd;
	size_t				size;
	net64_t				*ca_guids;

	/* Set defaults. */
	if( !__parse_options( argc, argv ) )
		return 1;

	status = ib_open_al( &h_al );
	if( status != IB_SUCCESS )
	{
		printf( "ib_open_al returned %s\n", ib_get_err_str( status ) );
		return 1;
	}

	size = 0;
	status = ib_get_ca_guids( h_al, NULL, &size );
	if( status != IB_INSUFFICIENT_MEMORY )
	{
		printf( "ib_get_ca_guids for array size returned %s",
			ib_get_err_str( status ) );
		goto done;
	}

	if( size == 0 )
	{
		printf( "No CAs installed.\n" );
		goto done;
	}

	ca_guids = malloc( sizeof(net64_t) * size );
	if( !ca_guids )
	{
		printf( "Failed to allocate CA GUID array.\n" );
		goto done;
	}

	status = ib_get_ca_guids( h_al, ca_guids, &size );
	if( status != IB_SUCCESS )
	{
		printf( "ib_get_ca_guids for CA guids returned %s",
			ib_get_err_str( status ) );
		free( ca_guids );
		goto done;
	}

	status = ib_open_ca( h_al, ca_guids[0], NULL, NULL, &h_ca );
	free( ca_guids );
	if( status != IB_SUCCESS )
	{
		printf( "ib_open_ca returned %s", ib_get_err_str( status ) );
		goto done;
	}

	status = ib_alloc_pd( h_ca, IB_PDT_NORMAL, NULL, &h_pd );
	if( status != IB_SUCCESS )
	{
		printf( "ib_alloc_pd returned %s", ib_get_err_str( status ) );
		goto done;
	}

	if( test_mr )
		__test_mr( h_pd );

	if( test_cq )
		__test_cq( h_ca, FALSE );

	if( test_resize )
		__test_cq( h_ca, TRUE );

	//if( test_qp )
	//	__test_qp( h_ca, h_pd );

done:
	ib_close_al( h_al );

	return 0;
}
