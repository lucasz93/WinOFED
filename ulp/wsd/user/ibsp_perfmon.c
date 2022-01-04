/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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


#include "ibspdebug.h"
#if defined(EVENT_TRACING)
#include "ibsp_perfmon.tmh"
#endif

#include <string.h>
#include "ibspdll.h"
#include "ibsp_perfmon.h"


struct _ibsp_pm_definition	g_ibsp_pm_def; /* IB WSD performance object */

struct _pm_stat g_pm_stat;

void
IBSPPmInit( void )
{
	HANDLE				h_mapping;
	BOOL				just_created;
	SECURITY_ATTRIBUTES	sec_attr;

	IBSP_ENTER( IBSP_DBG_PERFMON );

	g_pm_stat.idx = INVALID_IDX;
	g_pm_stat.p_shmem = NULL;

	sec_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
	sec_attr.bInheritHandle = FALSE;

	if( !ConvertStringSecurityDescriptorToSecurityDescriptor(
		IBSP_PM_SEC_STRING, SDDL_REVISION_1,
		&(sec_attr.lpSecurityDescriptor), NULL ) )
	{
		IBSP_ERROR( ("SecurityDescriptor error %d\n", GetLastError()) );
		return;
	}

	h_mapping = CreateFileMapping(
		INVALID_HANDLE_VALUE,	// use paging file
		&sec_attr,				// security attributes
		PAGE_READWRITE,			// read/write access
		0,						// size: high 32-bits
		sizeof(pm_shmem_t),		// size: low 32-bits
		IBSP_PM_MAPPED_OBJ_NAME );

	just_created = (GetLastError() != ERROR_ALREADY_EXISTS);

	LocalFree( sec_attr.lpSecurityDescriptor );

	if( h_mapping == NULL )
	{
		IBSP_ERROR_EXIT( ("CreateFileMapping error %d\n", GetLastError()) );
		return;
	}

	/* Get a pointer to the shared memory. */
	g_pm_stat.p_shmem = MapViewOfFile(
						h_mapping,     // object handle
						FILE_MAP_ALL_ACCESS,
						0,              // high offset:  map from
						0,              // low offset:   beginning
						0);             // num bytes to map

	/* Now that we have the view mapped, we don't need the mapping handle. */
	g_pm_stat.h_mapping = h_mapping;

	if( g_pm_stat.p_shmem == NULL )
	{
		IBSP_ERROR( ("MapViewOfFile returned %d\n", GetLastError()) );
		return;
	}
	
	if( just_created )
	{
		/*
		 * Reserve instance 0 for fallback counters
		 * Apps that can't get a dedicated slot will share this one.
		 */
		wcscpy( g_pm_stat.p_shmem->obj[0].app_name,
			IBSP_PM_TOTAL_COUNTER_NAME );
		g_pm_stat.p_shmem->obj[0].taken = 1;
	}

	IBSP_EXIT( IBSP_DBG_PERFMON );
}


/*
 * We always get a slot - either an individual one, or fall back on the 
 * common one.
 */
void
IBSPPmGetSlot( void )
{
	WCHAR		mod_path[MAX_PATH];
	WCHAR*		buf;
	int			idx;
	size_t		name_len;
	WCHAR		id_str[12];
	mem_obj_t	*p_slot;
	pm_shmem_t* p_mem = g_pm_stat.p_shmem;

	IBSP_ENTER( IBSP_DBG_PERFMON );

	if( g_pm_stat.p_shmem == NULL )
	{
		g_pm_stat.pdata = g_pm_stat.fall_back_data;
		return;
	}

	GetModuleFileNameW( NULL, mod_path, MAX_PATH );

	buf = wcsrchr( mod_path, L'\\' );
	if( !buf )
		buf = mod_path;
	else
		buf++;

	/* The max length is 11, one for the ':', and 10 for the process ID. */
	id_str[0] = ':';
	_ultow( GetCurrentProcessId(), &id_str[1], 10 );

	/* Cap the length of the application. */
	name_len = min( wcslen( buf ),
		IBSP_PM_APP_NAME_SIZE - 1 - wcslen( id_str ) );

	/* instance 0 is taken for "Total" counters, so don't try it */
	for( idx = 1; idx < IBSP_PM_NUM_INSTANCES; idx++)
	{
		/* Compare with 0, exchange with 1 */
		if( InterlockedCompareExchange(
			&g_pm_stat.p_shmem->obj[idx].taken, 1, 0 ) )
		{
			continue;
		}

		p_slot = &g_pm_stat.p_shmem->obj[idx];

		/* Copy the app name. */
		CopyMemory( p_slot->app_name, buf, name_len * sizeof(WCHAR) );
		CopyMemory( &p_slot->app_name[name_len], id_str,
			(wcslen( id_str ) + 1) * sizeof(WCHAR) );

		g_pm_stat.idx = idx;
		g_pm_stat.pdata = g_pm_stat.p_shmem->obj[idx].data;
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_PERFMON,
			("%S got slot %d\n", p_slot->app_name, idx) );
		break;
	}
	
	if( idx == IBSP_PM_NUM_INSTANCES )
	{
		/*
		 * Assign "Total" slot for this process to avoid loosing precious
		 * statistic.  Keep saved idx INVALID so data won't be flushed during
		 * process closeout.
		 */
		g_pm_stat.pdata = p_mem->obj[0].data;
	}
		
	IBSP_EXIT( IBSP_DBG_PERFMON );
}


void
IBSPPmReleaseSlot( void )
{
	mem_obj_t	*p_slot;
	int			idx;

	/* perfmon never get registered itself in shared mem buffer */
	if ( g_pm_stat.idx == INVALID_IDX )
		return;

	if( g_pm_stat.p_shmem == NULL )
		return;

	p_slot = &g_pm_stat.p_shmem->obj[g_pm_stat.idx];

	/* Add all the data to the "Total" bin (0) */
	for( idx = 0; idx < IBSP_PM_NUM_COUNTERS; idx++ )
	{
		InterlockedExchangeAdd64( &g_pm_stat.p_shmem->obj[0].data[idx],
			InterlockedExchange64( &g_pm_stat.pdata[idx], 0 ) );
	}
	ZeroMemory( p_slot->app_name, sizeof(p_slot->app_name) );
	InterlockedExchange( &p_slot->taken, 0 );

	g_pm_stat.idx = INVALID_IDX;

	IBSP_EXIT( IBSP_DBG_PERFMON );
}


static BOOL
__PmIsQuerySupported(
	IN				WCHAR*					p_query_str )
{
	if( p_query_str == NULL )
		return TRUE;

	if( *p_query_str == 0 )
		return TRUE;

	if( wcsstr( p_query_str, L"Global" ) != NULL )
		return TRUE;

	if( wcsstr( p_query_str, L"Foreign" ) != NULL )
		return FALSE;

	if( wcsstr( p_query_str, L"Costly" ) != NULL )
		return FALSE;

	else
		return TRUE;
}


/*
 * http://msdn.microsoft.com/library/en-us/perfctrs/perf/openperformancedata.asp
 */
DWORD APIENTRY
IBSPPmOpen(
	IN				LPWSTR						lpDeviceNames )
{
	DWORD status = ERROR_SUCCESS;
	HKEY  pm_hkey = NULL;
	DWORD data_size;
	DWORD data_type;
	DWORD first_counter = 0;
	DWORD first_help = 0;
	int num		= 0;
	int num_offset;

	IBSP_ENTER( IBSP_DBG_PERFMON );

	UNUSED_PARAM(lpDeviceNames);

	if( g_pm_stat.threads++ )
	{
		IBSP_EXIT( IBSP_DBG_PERFMON );
		return ERROR_SUCCESS;
	}

	/* open Registry and query for the first and last keys */
	status = RegOpenKeyEx( HKEY_LOCAL_MACHINE,
		IBSP_PM_REGISTRY_PATH IBSP_PM_SUBKEY_PERF,
		0L, KEY_READ, &pm_hkey);

	if( status != ERROR_SUCCESS )
	{
		g_pm_stat.threads--;
		IBSP_ERROR_EXIT( ("RegOpenKeyEx for perf information returned %d.\n", status) );
		return status;
	}

	data_size = sizeof(DWORD);
	status = RegQueryValueEx( pm_hkey, "First Counter", 0L,
		&data_type, (LPBYTE)&first_counter, &data_size );

	if( status != ERROR_SUCCESS )
	{
		RegCloseKey(pm_hkey);
		g_pm_stat.threads--;
		IBSP_ERROR_EXIT( ("RegQueryValueEx for \"First Counter\" returned %d.\n", status) );
		return status;
	}

	data_size = sizeof(DWORD);
	status = RegQueryValueEx( pm_hkey, "First Help", 0L,
		&data_type, (LPBYTE)&first_help, &data_size );

	RegCloseKey( pm_hkey );

	if( status != ERROR_SUCCESS )
	{
		g_pm_stat.threads--;
		IBSP_ERROR_EXIT( ("RegQueryValueEx for \"First Help\" returned %d.\n", status) );
		return status;
	}

	/* perf_obj */		
	g_ibsp_pm_def.perf_obj.ObjectNameTitleIndex  = IBSP_PM_OBJ + first_counter;
	g_ibsp_pm_def.perf_obj.ObjectHelpTitleIndex  = IBSP_PM_OBJ + first_help;
	g_ibsp_pm_def.perf_obj.TotalByteLength  =
		sizeof(ibsp_pm_definition_t) + sizeof(ibsp_pm_counters_t);
	g_ibsp_pm_def.perf_obj.DefinitionLength = sizeof(ibsp_pm_definition_t);
	g_ibsp_pm_def.perf_obj.HeaderLength     = sizeof(PERF_OBJECT_TYPE);

	g_ibsp_pm_def.perf_obj.ObjectNameTitle = 0;
	g_ibsp_pm_def.perf_obj.ObjectHelpTitle = 0;

	g_ibsp_pm_def.perf_obj.DetailLevel = PERF_DETAIL_NOVICE;
	g_ibsp_pm_def.perf_obj.NumCounters = IBSP_PM_NUM_COUNTERS;
	g_ibsp_pm_def.perf_obj.DefaultCounter = 0;
	g_ibsp_pm_def.perf_obj.NumInstances = 0;
	g_ibsp_pm_def.perf_obj.CodePage	= 0;

	QueryPerformanceFrequency( &g_ibsp_pm_def.perf_obj.PerfFreq );

	/* initialize all counter definitions */
	num_offset = IBSP_PM_OBJ + 2;
	for ( num = 0; num <  IBSP_PM_NUM_COUNTERS ; num++, num_offset += 2)
	{
		g_ibsp_pm_def.counter[num].CounterNameTitleIndex = num_offset + first_counter;
		g_ibsp_pm_def.counter[num].CounterHelpTitleIndex = num_offset + first_help;
		g_ibsp_pm_def.counter[num].ByteLength = sizeof(PERF_COUNTER_DEFINITION);
		g_ibsp_pm_def.counter[num].CounterNameTitle = 0;
		g_ibsp_pm_def.counter[num].CounterHelpTitle = 0;
		g_ibsp_pm_def.counter[num].DefaultScale = 0;
		g_ibsp_pm_def.counter[num].DetailLevel = PERF_DETAIL_NOVICE;
		g_ibsp_pm_def.counter[num].CounterType = PERF_COUNTER_BULK_COUNT;
		/* All counters should be kept to 64-bits for consistency and simplicity. */
		g_ibsp_pm_def.counter[num].CounterSize = sizeof(LONG64);
		g_ibsp_pm_def.counter[num].CounterOffset =
			(DWORD)offsetof( ibsp_pm_counters_t, data[num] );
	}

	g_pm_stat.h_evlog = RegisterEventSource( NULL, IBSP_PM_SUBKEY_NAME );
	if( !g_pm_stat.h_evlog )
	{
		g_pm_stat.threads--;
		status = GetLastError();
		IBSP_ERROR_EXIT( ("RegisterEventSource failed with %d\n", status) );
		return status;
	}

	IBSP_EXIT( IBSP_DBG_PERFMON );
	return ERROR_SUCCESS;
}


/*
 * http://msdn.microsoft.com/library/en-us/perfctrs/perf/closeperformancedata.asp
 */
DWORD APIENTRY
IBSPPmClose( void )
{
	BOOL status;

	IBSP_ENTER( IBSP_DBG_PERFMON );

	if( --g_pm_stat.threads )
	{
		IBSP_EXIT( IBSP_DBG_PERFMON );
		return ERROR_SUCCESS;
	}

	IBSPPmReleaseSlot();

	/* avoid double closing */
	if( g_pm_stat.p_shmem != NULL )
	{
		status = UnmapViewOfFile( g_pm_stat.p_shmem );
		g_pm_stat.p_shmem = NULL;
	}
	
	if( g_pm_stat.h_evlog != NULL )
	{
		DeregisterEventSource( g_pm_stat.h_evlog );
		g_pm_stat.h_evlog = NULL;
	}

	IBSP_EXIT( IBSP_DBG_PERFMON );
	return ERROR_SUCCESS;
}



/*
 * http://msdn.microsoft.com/library/en-us/perfctrs/perf/collectperformancedata.asp
 */
DWORD WINAPI
IBSPPmCollectData(
	IN				LPWSTR						lpValueName,
	IN	OUT			LPVOID*						lppData,
	IN	OUT			LPDWORD						lpcbTotalBytes,
	IN	OUT			LPDWORD						lpNumObjectTypes )
{
	int32_t						sh_num;
	int32_t						num_instances, max_instances;
	uint32_t					use_bytes;
	ibsp_pm_definition_t		*p_obj_def;
	ibsp_pm_counters_t			*p_count_def;
	PERF_INSTANCE_DEFINITION	*p_inst_def;
	pm_shmem_t					*p_mem;
	LONG64						total_data[IBSP_PM_NUM_COUNTERS];
	
	IBSP_ENTER( IBSP_DBG_PERFMON );

	p_mem = (pm_shmem_t *)g_pm_stat.p_shmem;
	
	if( p_mem == NULL )
	{
		IBSP_ERROR( ("No shared memory object\n") );
		goto done;
	}

	if( !__PmIsQuerySupported(lpValueName ) )
	{
		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_PERFMON, ("Unsupported query\n") );
		goto done;
	}

	if( !g_pm_stat.threads )
	{
		IBSP_ERROR( ("Initialization was not completed\n") );
done:
		*lpcbTotalBytes   = 0;
		*lpNumObjectTypes = 0;

		IBSP_EXIT( IBSP_DBG_PERFMON );
		return ERROR_SUCCESS;
	}

	ZeroMemory( &total_data, sizeof(total_data) );
	num_instances = 0;
	/* sum total counters that were not filled in completion routine */
	for( sh_num = 0; sh_num < IBSP_PM_NUM_INSTANCES; sh_num++ )
	{
		if( !InterlockedCompareExchange( &p_mem->obj[sh_num].taken, 1, 1 ) )
			continue;

		total_data[BYTES_SEND] += p_mem->obj[sh_num].data[BYTES_SEND];
		total_data[BYTES_RECV] += p_mem->obj[sh_num].data[BYTES_RECV];
		total_data[BYTES_WRITE] += p_mem->obj[sh_num].data[BYTES_WRITE];
		total_data[BYTES_READ] += p_mem->obj[sh_num].data[BYTES_READ];
		/* Update total for current slot. */
		p_mem->obj[sh_num].data[BYTES_TOTAL] =
			p_mem->obj[sh_num].data[BYTES_SEND] +
			p_mem->obj[sh_num].data[BYTES_RECV] +
			p_mem->obj[sh_num].data[BYTES_WRITE] +
			p_mem->obj[sh_num].data[BYTES_READ];
		total_data[BYTES_TOTAL] += p_mem->obj[sh_num].data[BYTES_TOTAL];
		total_data[COMP_SEND] += p_mem->obj[sh_num].data[COMP_SEND];
		total_data[COMP_RECV] += p_mem->obj[sh_num].data[COMP_RECV];
		total_data[COMP_TOTAL] += p_mem->obj[sh_num].data[COMP_TOTAL];
		total_data[INTR_TOTAL] += p_mem->obj[sh_num].data[INTR_TOTAL];

		num_instances++;
	}

	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_PERFMON, ("%d instances.\n", num_instances) );

	/* calc buffer size required for data return */
	use_bytes = sizeof(ibsp_pm_definition_t) + \
				(sizeof(PERF_INSTANCE_DEFINITION) + \
				sizeof(ibsp_pm_counters_t) + \
				(sizeof(WCHAR) * IBSP_PM_APP_NAME_SIZE)) * num_instances;

	if( *lpcbTotalBytes < use_bytes )
	{
		*lpcbTotalBytes   = 0;
		*lpNumObjectTypes = 0;
		return ERROR_MORE_DATA;
	}

	p_obj_def = (ibsp_pm_definition_t*)*lppData;
	use_bytes = sizeof(ibsp_pm_definition_t);

	/* Copy counter definition */
	CopyMemory( p_obj_def, &g_ibsp_pm_def, sizeof(ibsp_pm_definition_t) );

	p_obj_def->perf_obj.NumInstances = num_instances;
	QueryPerformanceCounter( &p_obj_def->perf_obj.PerfTime );

	max_instances = num_instances;

	/* Assign pointers for the first instance */
	p_inst_def = (PERF_INSTANCE_DEFINITION*)(p_obj_def + 1);

	for( sh_num = 0; sh_num < IBSP_PM_NUM_INSTANCES; sh_num++ )
	{
		if( !InterlockedCompareExchange( &p_mem->obj[sh_num].taken, 1, 1 ) )
			continue;

		/* Make sure we don't overrun the buffer! */
		if( max_instances-- == 0 )
			break;

		p_inst_def->ByteLength = sizeof(PERF_INSTANCE_DEFINITION) + 
			(sizeof(WCHAR) * IBSP_PM_APP_NAME_SIZE);
		p_inst_def->ParentObjectTitleIndex = 0;
		p_inst_def->ParentObjectInstance = 0;
		p_inst_def->UniqueID = -1;  /* using module names */
		p_inst_def->NameOffset = sizeof(PERF_INSTANCE_DEFINITION);

		/* Length in bytes of Unicode name string, including terminating NULL */
		p_inst_def->NameLength =
			(DWORD)wcslen( p_mem->obj[sh_num].app_name ) + 1;
		p_inst_def->NameLength *= sizeof(WCHAR);

		CopyMemory( (WCHAR*)(p_inst_def + 1),
			p_mem->obj[sh_num].app_name, p_inst_def->NameLength );

		use_bytes += p_inst_def->ByteLength;

		/* advance to counter definition */
		p_count_def = (ibsp_pm_counters_t*)
			(((BYTE*)p_inst_def) + p_inst_def->ByteLength);

		p_count_def->pm_block.ByteLength = sizeof(ibsp_pm_counters_t);
		use_bytes += sizeof(ibsp_pm_counters_t);

		/* Here we report actual counter values. */
		if( sh_num == 0 )
		{
			CopyMemory( p_count_def->data, total_data, sizeof(total_data) );
		}
		else
		{
			CopyMemory( p_count_def->data, p_mem->obj[sh_num].data,
				sizeof(p_mem->obj[sh_num].data) );
		}

		/* Advance pointers for the next instance definition */
		p_inst_def = (PERF_INSTANCE_DEFINITION*)(p_count_def + 1);
	}

	p_obj_def->perf_obj.TotalByteLength = (DWORD)use_bytes;

	*lppData = ((BYTE*)*lppData) + use_bytes;
	*lpNumObjectTypes = 1;
	*lpcbTotalBytes = (DWORD)use_bytes;

	IBSP_EXIT( IBSP_DBG_PERFMON );
	return ERROR_SUCCESS;
}
