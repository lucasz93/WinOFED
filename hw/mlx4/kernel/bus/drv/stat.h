#pragma once

/*++

Copyright (c) 2005-2009 Mellanox Technologies. All rights reserved.

Module Name:
	stat.h

Abstract:
	Statistics Collector header file

Revision History:

Notes:

--*/

#pragma warning(disable:4324)
#include <wdf.h>
#pragma warning(default:4324)
#include "l2w.h"
#include "ib_pack.h"
#include "qp.h"
#include "device.h"

//
// restrictions
//

#define MLX4_ST_MAX_DEVICES			80
#define MLX4_ST_MAX_CARDS			64

//
// enums
// 

#define MLX4_MAD_TRACE_LRH			(1 << 0)
#define MLX4_MAD_TRACE_BTH			(1 << 1)
#define MLX4_MAD_TRACE_DETH			(1 << 2)
#define MLX4_MAD_TRACE_GRH			(1 << 3)
#define MLX4_MAD_TRACE_WQE			(1 << 4)
#define MLX4_MAD_TRACE_UDH			(1 << 5)
#define MLX4_MAD_TRACE_WR			(1 << 6)
#define MLX4_MAD_TRACE_MLX_WQE_DUMP	(1 << 7)



//
// structures
//

// Communication Channel

typedef struct _MLX4_SLAVE_COMM_CMD_INFO {
	u64 time;
	u32  cmd_count;
	u8	 slaveIssued;
}MLX4_SLAVE_COMM_CMD_INFO, *PMLX4_SLAVE_COMM_CMD_INFO;

typedef struct _MLX4_MASTER_COMM_CMD_INFO {
	u64 time_to_call;
	u64 time_to_process;
	u32 cmd_count;
	u8 slave_id;
	u8 cmd;
} MLX4_MASTER_COMM_CMD_INFO ,*PMLX4_MASTER_COMM_CMD_INFO;

typedef struct _MLX4_COMM_CMD_INFO {
	#define MASTER_POOL_SIZE 1024		//should be power of 2
	
	int cmd_indx;
	MLX4_SLAVE_COMM_CMD_INFO slave_comm_dbg[MLX4_MAX_NUM_SLAVES];
	MLX4_MASTER_COMM_CMD_INFO master_comm_dbg[MASTER_POOL_SIZE];
} MLX4_COMM_CMD_INFO, *PMLX4_COMM_CMD_INFO;


// thread

typedef struct _MLX4_ST_THREAD
{
	PKTHREAD			p_thread;
	char				thread_name[24];
	
} MLX4_ST_THREAD, *PMLX4_ST_THREAD;

// device
typedef struct _MLX4_ST_CARD *PMLX4_ST_CARD;

typedef struct _FDO_DEVICE_DATA *PFDO_DEVICE_DATA;

typedef struct _MLX4_ST_DEVICE
{
	boolean_t			valid;
	PFDO_DEVICE_DATA	p_fdo;
	PDEVICE_OBJECT		pdo;
	bool				added_to_removal_dep;
	WDFDEVICE			h_wdf_device;
	ULONG				flags;
	MLX4_ST_THREAD		thread_comm;	/* Master/Slave thread to work with communication channel */
	struct mlx4_priv	*priv;
	// interrupt handling statictics
	ULONG				isr_n_total;		/* total number of interrupt calls */
	ULONG				isr_n_calls;		/* number of our interrupts */
	ULONG				isr_n_dpcs;			/* number of scheduled DPCs */
	ULONG				isr_n_isrs;			/* number of called user ISRs */
	ULONG				dpc_n_calls;		/* number of called user ISRs */
	ULONG				dpc_eq_ci_first;	/* consumer index at last entry to dpc */
	ULONG				dpc_eq_ci_last;		/* consumer index at last exit from dpc */
	struct mlx4_eq 		*dpc_eq;			/* last eq handled */
	ULONG				cmd_n_events;		/* number of events, i.e. completed commands */
	ULONG				cmd_n_events_to;	/* number of timed-out events */
	// memory usage statistics
	ULONG				n_cont_allocs;		/* number of contiguous allocations */
	ULONG				total_cont_allocs;	/* total size of contiguous allocations */
	ULONG				n_cont_frees;		/* number of contiguous releases */
	ULONG				total_cont_frees;	/* total size of contiguous releases */
	USHORT				func_num;			/* function number in Flex10 */
	PMLX4_ST_CARD		p_card;				/* card slot, it belongs to */
	
} MLX4_ST_DEVICE, *PMLX4_ST_DEVICE;

// card
// driver
typedef struct _MLX4_ST_CARD
{
	boolean_t			valid;
	int					bus_num;	
	int					n_slaves;
	MLX4_ST_DEVICE		dev[MLX4_ST_MAX_DEVICES];
	
} MLX4_ST_CARD, *PMLX4_ST_CARD;

#define ET_MAX_PREFIX_SIZE	128
#define ET_MAX_EVENT_SIZE	256
#define ET_MAX_REC_SIZE		(ET_MAX_PREFIX_SIZE+ET_MAX_EVENT_SIZE)
#define ET_BUF_SIZE			(ET_MAX_REC_SIZE * 1024)
#define ET_MAX_PRINT		100

typedef struct _et_hdr
{
	spinlock_t			lock;
	char				*ptr;
	char				*buf_end;
	char				*buf;
	int					info_size;
	int					buf_size;
	int					wraps;
	
} et_hdr_t, *pet_hdr_t;

#define MM_BUF_SIZE			1024

typedef struct _mm_list
{
	LIST_ENTRY			list_head;
	int					cnt;
	
} mm_list_t;

typedef struct _mm_hdr
{
	spinlock_t			lock;
	int					n_bufs;
	int					n_lists;
	int					n_too_large;	// asked allocation of more than MM_BUF_SIZE-1 pages
	boolean_t			enable_cache;
	mm_list_t			list[MM_BUF_SIZE];
	
} mm_hdr_t, *pmm_hdr_t;

// driver
typedef struct _MLX4_ST_DRIVER
{
	GLOBALS				*p_globals;
	WDFDRIVER			h_wdf_driver;	
	int					n_cards;
	et_hdr_t			et_hdr;
	mm_hdr_t			mm_hdr;
	
} MLX4_ST_DRIVER, *PMLX4_ST_DRIVER;

// driver stack

typedef struct _MLX4_ST_STAT
{
	MLX4_ST_DRIVER		drv;
	MLX4_COMM_CMD_INFO	comm_dbg;
	MLX4_ST_CARD		card[MLX4_ST_MAX_CARDS];
	
} MLX4_ST_STAT, *PMLX4_ST_STAT;


extern MLX4_ST_STAT g_stat;
#define SLAVE_COMM_INFO(i) g_stat.comm_dbg.slave_comm_dbg[i]
#define MASTER_COMM_INFO(i) g_stat.comm_dbg.master_comm_dbg[i]

void st_dev_rmv( PMLX4_ST_DEVICE p_stat );

PMLX4_ST_DEVICE st_dev_add(int bus, int func);
PMLX4_ST_DEVICE st_dev_get(int bus, int func);

void st_dev_add_thread( PMLX4_ST_DEVICE p_stat, char *name );

void st_dev_add_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size );

void st_dev_rmv_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size );

void st_et_event_func( const char * const  Caller, const char * const  Format, ... );

#define st_et_event(_Format, ...)	st_et_event_func( __FUNCTION__, _Format, __VA_ARGS__ )

void st_et_report();

void st_et_init();

void st_et_deinit();

PVOID st_mm_alloc(ULONG size);

boolean_t st_mm_free(PVOID ptr, ULONG size);

void st_mm_enable_cache(boolean_t enable);

void st_mm_report(char* title);

void st_mm_init();
  
void st_mm_deinit();

