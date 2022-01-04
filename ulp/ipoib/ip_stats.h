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


#ifndef _IP_STATS_H_
#define _IP_STATS_H_


#include <complib/cl_types.h>


/****s* IB Network Drivers/ip_data_stats_t
* NAME
*	ip_data_stats_t
*
* DESCRIPTION
*	Defines data transfer statistic information for an IP device.
*
* SYNOPSIS
*/
typedef struct _ip_data_stats
{
	uint64_t		bytes;
	uint64_t		frames;

}	ip_data_stats_t;
/*
* FIELDS
*	bytes
*		Total number of bytes transfered.
*
*	frames
*		Total number of frames transfered.
*
* SEE ALSO
*	IPoIB, INIC, ip_comp_stats_t, ip_stats_t
*********/


/****s* IB Network Drivers/ip_comp_stats_t
* NAME
*	ip_comp_stats_t
*
* DESCRIPTION
*	Defines transfer completion statistic information for an IP device.
*
* SYNOPSIS
*/
typedef struct _ip_comp_stats
{
	uint64_t		success;
	uint64_t		error;
	uint64_t		dropped;

}	ip_comp_stats_t;
/*
* FIELDS
*	success
*		Total number of requests transfered successfully.
*
*	error
*		Total number of requests that failed being transfered.
*
*	dropped
*		Total number of requests that were dropped.
*
* SEE ALSO
*	IPoIB, INIC, ip_data_stats_t, ip_stats_t
*********/


/****s* IB Network Drivers/ip_stats_t
* NAME
*	ip_stats_t
*
* DESCRIPTION
*	Defines statistic information for an IP device.
*
* SYNOPSIS
*/
typedef struct _ip_stats
{
	ip_comp_stats_t		comp;
	ip_data_stats_t		ucast;
	ip_data_stats_t		bcast;
	ip_data_stats_t		mcast;

}	ip_stats_t;
/*
* FIELDS
*	comp
*		Request completion statistics.
*
*	ucast
*		Data statistics for unicast packets
*
*	bcast
*		Data statistics for broadcast packets
*
*	mcast
*		Data statistics for multicast packets
*
* SEE ALSO
*	IPoIB, INIC, ip_data_stats_t, ip_comp_stats_t
*********/


typedef enum _ip_stat_sel
{
	IP_STAT_SUCCESS,
	IP_STAT_ERROR,
	IP_STAT_DROPPED,
	IP_STAT_UCAST_BYTES,
	IP_STAT_UCAST_FRAMES,
	IP_STAT_BCAST_BYTES,
	IP_STAT_BCAST_FRAMES,
	IP_STAT_MCAST_BYTES,
	IP_STAT_MCAST_FRAMES

}	ip_stat_sel_t;

#endif	/* _IP_STATS_H_ */
