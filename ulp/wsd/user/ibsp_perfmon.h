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

#ifndef _IBSP_PERFMON_H_
#define _IBSP_PERFMON_H_


#include <sddl.h>
#include <winperf.h>
#include "wsd/ibsp_regpath.h"


/* invalid instance index value to initialize */
#define INVALID_IDX			0xffffffff

#define IBSP_PM_SEC_STRING \
	TEXT("D:(A;CIOI;GAFA;;;WD)")		/* SDDL_EVERYONE */

#define IBSP_PM_NUM_OBJECT_TYPES	1
#define IBSP_PM_NUM_INSTANCES		100	/* how many processes we can handle */

#define IBSP_PM_APP_NAME_SIZE		24	/* Must be multiple of 8 */
#define IBSP_PM_TOTAL_COUNTER_NAME	L"_Total"
#define IBSP_PM_MAPPED_OBJ_NAME		TEXT("Global\\ibwsd_perfmon_data")


/* Structures used to report counter information to perfmon. */
typedef struct _ibsp_pm_definition
{
	PERF_OBJECT_TYPE			perf_obj;
	PERF_COUNTER_DEFINITION		counter[IBSP_PM_NUM_COUNTERS];

}	ibsp_pm_definition_t;

typedef struct _ibsp_pm_counters
{
	PERF_COUNTER_BLOCK	pm_block;
	LONG64				data[IBSP_PM_NUM_COUNTERS];

}	ibsp_pm_counters_t;


/* Structures used to manage counters internally */
typedef struct _mem_obj
{
	volatile LONG	taken;
	WCHAR			app_name[IBSP_PM_APP_NAME_SIZE];
	LONG64			data[IBSP_PM_NUM_COUNTERS];

}	mem_obj_t;

typedef struct _pm_shmem
{
	mem_obj_t		obj[IBSP_PM_NUM_INSTANCES];

}	pm_shmem_t;


/* global data for every process linked to this DLL */
struct _pm_stat
{
	struct _pm_shmem*		p_shmem;		/* base pointer to shared memory for this process */
	volatile LONG64*		pdata;			/* pointer to data collected */
	HANDLE					h_mapping;
	HANDLE					h_evlog;		/* event log handle */
	DWORD					threads;		/* number of threads open */
	DWORD					idx;			/* slot index assigned for this process */
	LONG64					fall_back_data[IBSP_PM_NUM_COUNTERS];

};

extern struct _pm_stat g_pm_stat;


void
IBSPPmInit( void );

DWORD APIENTRY
IBSPPmClose( void );

void
IBSPPmGetSlot( void );

void
IBSPPmReleaseSlot( void );

#endif /* _IBSP_PERFMON_H_ */
