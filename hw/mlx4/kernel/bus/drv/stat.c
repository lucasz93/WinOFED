/*++

Copyright (c) 2005-2009 Mellanox Technologies. All rights reserved.

Module Name:
	stat.h

Abstract:
	Statistics Collector header file

Revision History:

Notes:

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "stat.tmh"
#endif


MLX4_ST_STAT g_stat;

static void __print_grh( struct mlx4_dev *mdev, struct ib_unpacked_grh *p)
{
    UNUSED_PARAM(mdev);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== GRH ==========\n"));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t ip_version        %02x\n", p->ip_version));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t traffic_class     %02x\n", p->traffic_class));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t flow_label        %08x\n", 
		be32_to_cpu(p->flow_label)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t payload_length    %04x\n", 
		be16_to_cpu(p->payload_length)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t next_header       %02x\n", p->next_header));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t hop_limit         %02x\n", p->hop_limit));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t source_gid        %08I64x:%08I64x\n", 
		be64_to_cpu(p->source_gid.global.subnet_prefix),
		be64_to_cpu(p->source_gid.global.interface_id)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t dest_gid        %08I64x:%08I64x\n", 
		be64_to_cpu(p->destination_gid.global.subnet_prefix),
		be64_to_cpu(p->destination_gid.global.interface_id)));
}

static void __print_deth( struct mlx4_dev *mdev, struct ib_unpacked_deth *p)
{
    UNUSED_PARAM(mdev);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== DETH ==========\n"));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t qkey              %08x\n", 
		be32_to_cpu(p->qkey)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t source_qpn        %08x\n", 
		be32_to_cpu(p->source_qpn)));
}

static void __print_bth( struct mlx4_dev *mdev, struct ib_unpacked_bth *p)
{
    UNUSED_PARAM(mdev);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== BTH ==========\n"));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t opcode            %02x\n", p->opcode));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t solicited_event   %02x\n", p->solicited_event));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t mig_req           %02x\n", p->mig_req));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t header_version    %02x\n", p->transport_header_version));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t pkey              %04x\n", 
		be16_to_cpu(p->pkey)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t destination_qpn   %08x\n", 
		be32_to_cpu(p->destination_qpn)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t ack_req           %02x\n", p->ack_req));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t psn               %08x\n", 
		be32_to_cpu(p->psn)));
}

static void __print_lrh( struct mlx4_dev *mdev, struct ib_unpacked_lrh *p)
{
    UNUSED_PARAM(mdev);
    MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== LRH ==========\n"));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t virtual_lane      %02x\n", p->virtual_lane));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t link_version      %02x\n", p->link_version));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t service_level     %02x\n", p->service_level));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t link_next_header  %02x\n", p->link_next_header));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t destination_lid   %04x\n", 
		be16_to_cpu(p->destination_lid)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t packet_length     %04x\n", 
		be16_to_cpu(p->packet_length)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t source_lid        %04x\n", 
		be16_to_cpu(p->source_lid)));
}

static void __print_ud_header( struct mlx4_dev *mdev, struct ib_ud_header *p)
{
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== UD HEADER ==========\n"));

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t grh_present %d, imm_present %d, imm %08x\n",
		p->grh_present, p->immediate_present, be32_to_cpu(p->immediate_data) ));

	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_LRH )
		__print_lrh( mdev, &p->lrh );

	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_BTH )
		__print_bth( mdev, &p->bth );
	
	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_DETH )
		__print_deth( mdev, &p->deth );
	
	if ( p->grh_present && (mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_GRH) )
		__print_grh( mdev, &p->grh );
	
}

static void __print_mlx( struct mlx4_dev *mdev, struct mlx4_wqe_mlx_seg *p)
{
    UNUSED_PARAM(mdev);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== MLX WQE ==========\n"));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t owner             %02x", p->owner));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t opcode            %02x", p->opcode));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t size              %02x", p->size));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t flags             %08x", 
		be32_to_cpu(p->flags)));
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t rlid              %04x", 
		be16_to_cpu(p->rlid)));
}

void st_print_mlx_header( struct mlx4_dev *mdev, struct mlx4_ib_sqp *sqp, struct mlx4_wqe_mlx_seg *mlx )
{
	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_UDH )
		__print_ud_header( mdev, &sqp->hdr.ib );
	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_WQE )
		__print_mlx( mdev, mlx );
}

void st_print_mlx_send(struct mlx4_dev *mdev, struct ib_qp *ibqp, ib_send_wr_t *wr)
{
	struct mlx4_ib_qp *qp = to_mqp(ibqp);

	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_WR ) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== SEND WR on QP %#x (%#x) ==========\n",
			ibqp->qp_num, qp->mqp.qpn ));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t wr_type           %d\n", wr->wr_type));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t wr_id             %08I64x\n", wr->wr_id));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t send_opt          %x\n", wr->send_opt));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t immediate_data    %x\n", be32_to_cpu(wr->immediate_data)));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t num_ds            %d\n", wr->num_ds));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t ds[0].pa          %I64x\n", wr->ds_array[0].vaddr));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t ds[0].length      %x\n", wr->ds_array[0].length));
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\t ds[0].lkey        %x\n", wr->ds_array[0].lkey));
	}
}

void st_dump_mlx_wqe(struct mlx4_dev *mdev, void *wqe, int size_in_dwords, ib_send_wr_t *wr)
{
	int j;
	u32 *ptr = (u32*)wqe;
#if 0	
	int i, size;
#else	
	UNUSED_PARAM(wr);
#endif
	
	if ( mdev->pdev->p_stat->flags & MLX4_MAD_TRACE_MLX_WQE_DUMP ) {

		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "\n\t ========== MLX WQE at %p, size %#x ==========\n",
			wqe, size_in_dwords*4 ));
		
		for ( j = 0; j < size_in_dwords; ++j ) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "\t %04x:	%08x %08x %08x %08x \n", 16*j, 
				be32_to_cpu(ptr[4*j]), be32_to_cpu(ptr[4*j + 1]),
				be32_to_cpu(ptr[4*j + 2]),be32_to_cpu(ptr[4*j + 3]) ));
		}
	}

#if 0
	for ( j = 0; j < (int)wr->num_ds; ++j ) {
		mlx4_dbg(mdev, "\n\t ========== SMP %d at pa %I64x, size %#x, lkey %#x ==========\n",
			j, wr->ds_array[0].vaddr, wr->ds_array[0].length, wr->ds_array[0].lkey );

		//TODO: vaddr should be converted to virtual address
		ptr = (PVOID)(ULONG_PTR)wr->ds_array[0].vaddr;
		size = (wr->ds_array[0].length + 3) >> 2;
		for ( i = 0; i < size; ++i ) {
			mlx4_warn( mdev, "%04x:	%08x %08x %08x %08x \n", 16*i, 
				be32_to_cpu(ptr[4*i]), be32_to_cpu(ptr[4*i + 1]),
				be32_to_cpu(ptr[4*i + 2]),be32_to_cpu(ptr[4*i + 3]) );
		}
	}
#endif	

}

void st_dev_add_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size )
{
	if (p_stat) {
		p_stat->n_cont_allocs++;
		p_stat->total_cont_allocs += size;
	}
}

void st_dev_rmv_cont_mem_stat( PMLX4_ST_DEVICE p_stat, ULONG size )
{
	if (p_stat) {
		p_stat->n_cont_frees++;
		p_stat->total_cont_frees += size;
	}
}

void st_dev_add_thread( PMLX4_ST_DEVICE p_stat, char *name )
{
	if ( p_stat ) {
		if ( name ) {
			RtlStringCbCopyA( p_stat->thread_comm.thread_name, 
				sizeof(p_stat->thread_comm.thread_name), name );
			p_stat->thread_comm.p_thread = KeGetCurrentThread();
		}
		else {
			p_stat->thread_comm.p_thread = NULL;
			p_stat->thread_comm.thread_name[0] = '\0';
		}
	}
}

void st_dev_rmv( PMLX4_ST_DEVICE p_stat )
{
	if ( p_stat && p_stat->valid ) {
		p_stat->valid = FALSE;
		if ( p_stat->p_card->n_slaves-- == 1 ) {
			p_stat->p_card->valid = FALSE;
			g_stat.drv.n_cards--;
		}
	}
}

PMLX4_ST_DEVICE st_dev_get(int bus, int func)
{
	PMLX4_ST_CARD p_card = NULL;
	int i, found = FALSE;

	// sanity check
	if (func >= MLX4_ST_MAX_DEVICES) {
		cl_msg_out( "Incorrect function number %d (max is %d) \n", func, MLX4_ST_MAX_DEVICES );
		return NULL;
	}

	// look for a card slot in existing ones
	for ( i = 0 ; i < MLX4_ST_MAX_CARDS; ++i ) {
		p_card = &g_stat.card[i];
		if ( p_card->valid ) {
			if ( p_card->bus_num != bus )
				continue;
			found = TRUE;
			break;
		}
	}

	if ( found ) 
		return &p_card->dev[func];
	else
		return NULL;
}

PMLX4_ST_DEVICE st_dev_add(int bus, int func)
{
	PMLX4_ST_DEVICE p_stat;
	PMLX4_ST_CARD p_card = NULL;
	int i, found = FALSE;

	// sanity check
	if (func >= MLX4_ST_MAX_DEVICES) {
		cl_msg_out( "Incorrect function number %d (max is %d) \n", func, MLX4_ST_MAX_DEVICES );
		return NULL;
	}

	// look for a card slot in existing
	for ( i = 0 ; i < MLX4_ST_MAX_CARDS; ++i ) {
		p_card = &g_stat.card[i];
		if ( p_card->valid ) {
			if ( p_card->bus_num != bus )
				continue;
			found = TRUE;
			break;
		}
	}

	// allocate a new card slot
	if ( !found ) {
		for ( i = 0 ; i < MLX4_ST_MAX_CARDS; ++i ) {
			p_card = &g_stat.card[i];
			if ( !p_card->valid ) {
				p_card->valid = TRUE;
				p_card->bus_num = bus;
				g_stat.drv.n_cards++;
				found = TRUE;
				break;
			}
		}
	}		

	// failed card slot search
	if ( !found ) {
		cl_msg_out( "Not found place for globals statistics: n_cards %d (max is %d, bus %d, func %d) \n", 
			g_stat.drv.n_cards, MLX4_ST_MAX_CARDS, bus, func );
		return NULL;
	}	

	// find device slot
	p_stat = &p_card->dev[func];
	ASSERT( p_stat->valid == FALSE );
	memset( p_stat, 0, sizeof(p_card->dev[func]));
	p_stat->valid = TRUE;
	p_stat->func_num = (USHORT)func;
	p_stat->p_card = p_card;
	p_card->n_slaves++;

	return p_stat;
}

void st_et_event_func(
	IN const char * const  Caller,
	IN const char * const  Format, ...
	)
{
	va_list p_arg;
	pet_hdr_t p_hdr = &g_stat.drv.et_hdr;
	char msg[ET_MAX_EVENT_SIZE], prefix[ET_MAX_PREFIX_SIZE];
	size_t size, size1;
	NTSTATUS status;
	char *ptr;
	uint64_t time = cl_get_time_stamp();

	if ( !p_hdr->buf )
		return;

	spin_lock( &p_hdr->lock );

	// prepare user message
	va_start(p_arg, Format);
	msg[0] = '\0';
	status = RtlStringCbVPrintfA( &msg[0], sizeof(msg), Format , p_arg);
	ASSERT(!status);
	va_end(p_arg);

	// prepare the prefix
	prefix[0] = '\0';
	status = RtlStringCbPrintfA(&prefix[0], sizeof(prefix), "%I64u %-32.32s ", time, Caller);
	ASSERT(!status);

	// calculate the needed size
	size = 0;
	status = RtlStringCbLengthA( prefix, sizeof(prefix), &size1 );
	ASSERT(!status);
	size += size1;
	status = RtlStringCbLengthA( msg, sizeof(msg), &size1 );
	ASSERT(!status);
	size += size1 + 2;

	// calculate where to write
	ptr = p_hdr->ptr;
	if (ptr + size >= p_hdr->buf_end) {
		ptr = p_hdr->buf;
		p_hdr->wraps++;
	}

	// write the record
	ptr[0] = '\0';
	RtlStringCbPrintfA( &ptr[0], size, "%s%s", prefix, msg);
	ptr[size-1] = '\0';	// end of  buffer
	ptr += size - 1;
	p_hdr->ptr = ptr;
	p_hdr->info_size = (int)(p_hdr->ptr - p_hdr->buf);

	spin_unlock( &p_hdr->lock );
}

static void __et_helper()
{
	st_et_event( "// Some Eth/XoIB Flags (fMP_PORT_xxx)    \n");
	st_et_event( "// REGISTER_EVENT              0x00008000\n");
	st_et_event( "// RESET_FAILED                0x00100000\n");
	st_et_event( "// RESET_COMPLETE              0x00200000\n");
	st_et_event( "// LOW_POWER_IN_PROGRESS       0x00800000\n");
	st_et_event( "// NON_RECOVER_ERROR           0x01000000\n");
	st_et_event( "// HARDWARE_ERROR              0x02000000\n");
	st_et_event( "// BUS_DRIVER_INITIATED_RESET  0x04000000\n");
	st_et_event( "// NDIS_INITIATED_RESET        0x08000000\n");
	st_et_event( "// BUS_DRIVER_INITIATED_RMV    0x10000000\n");
	st_et_event( "// PAUSED                      0x20000000\n");
	st_et_event( "// RUNING                      0x40000000\n");
	st_et_event( "// HALT_IN_PROGRESS            0x80000000\n");
#if 0
	st_et_event( "// Some Eth/XoIB Events (IB_AE_xxx)\n" );
	st_et_event( "// PORT_ACTIVE                 0x15\n");
	st_et_event( "// PORT_DOWN                   0x16\n");
	st_et_event( "// CLIENT_REREGISTER           0x17\n");
	st_et_event( "// RESET_DRIVER                0x1b\n");
	st_et_event( "// RESET_CLIENT                0x1c\n");
	st_et_event( "// RESET_END                   0x1d\n");
	st_et_event( "// RESET_FAILED                0x1e\n");
	st_et_event( "// RESET_4_RMV                 0x23\n");
#endif	
}

void st_et_report()
{

	int i;
	pet_hdr_t p_hdr = &g_stat.drv.et_hdr;
	int skip;
	char *ptr;
	NTSTATUS status;
	size_t size;
  
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"\n=====================================================================================\n"));

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"Event Trace Report: buf %p, buf_size %d, cur_ptr %p, info_size %d, wraps %d\n",
		p_hdr->buf, p_hdr->buf_size, p_hdr->ptr, p_hdr->info_size, p_hdr->wraps ));

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"=====================================================================================\n\n"));

	// sanity checks
	if ( !p_hdr->buf )
		return;

	// count the number of messages
	ptr = p_hdr->buf;
	for ( i = 0; ptr < p_hdr->ptr; i++ ) {
		status = RtlStringCbLengthA( ptr, ET_MAX_REC_SIZE, &size );
		ASSERT(!status);
		if (!size) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				("Internal error: all records are skipped ??\n"));
			return;
		}
		ptr += size + 1;
	}
	
	// skip
	skip = (i > ET_MAX_PRINT) ? (i - ET_MAX_PRINT) : 0;
	ptr = p_hdr->buf;
	for ( i = 0; i < skip; i++ ) {
		status = RtlStringCbLengthA( ptr, ET_MAX_REC_SIZE, &size );
		ASSERT(!status);
		if (!size) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				("Internal error: all records are skipped ??\n"));
			return;
		}
		ptr += size + 1;
	}
	
 	// print
	for ( i = 0; ptr < p_hdr->ptr; i++ ) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
			"%02d. %s", i, ptr ));
		status = RtlStringCbLengthA( ptr, ET_MAX_REC_SIZE, &size );
		ASSERT(!status);
		if (!size) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( 
				"Internal error: all records are printed ??\n"));
			return;
		}
		ptr += size + 1;
	}
 }

void st_et_init()
{
	char *ptr;
	 
	spin_lock_init( &g_stat.drv.et_hdr.lock );
	g_stat.drv.et_hdr.buf_size = ET_BUF_SIZE;
	g_stat.drv.et_hdr.info_size = 0;
	g_stat.drv.et_hdr.wraps = 0;
	ptr = (char*)ExAllocatePoolWithTag( NonPagedPool, ET_BUF_SIZE, 'FBTE' );
	if (ptr) {
		RtlZeroMemory( ptr, ET_BUF_SIZE );
		g_stat.drv.et_hdr.buf = ptr;
		g_stat.drv.et_hdr.ptr = ptr;
		g_stat.drv.et_hdr.buf_end = ptr + ET_BUF_SIZE - 1;
		__et_helper();
	}
}

static void __st_test_wraparound()
{
	int i, loops = 100000;
	char buf[] = "abcdefghijklmnopqrstuvwxyz123456789\n";
	for (i = 0; i < loops; ++i)
		st_et_event( buf );
}

void st_et_deinit()
{
	if (g_stat.drv.et_hdr.buf) {
//		__st_test_wraparound();
		st_et_report();
		ExFreePoolWithTag(g_stat.drv.et_hdr.buf, 'FBTE' );
		g_stat.drv.et_hdr.buf = NULL;
	}
}

PVOID st_mm_alloc(ULONG size)
{
	void *ptr = NULL;
	mm_list_t *p_list;
	pmm_hdr_t p_hdr = &g_stat.drv.mm_hdr;
	ULONG n_pages = BYTES_TO_PAGES(size);

	ASSERT( n_pages > 0 );
	
	if ( n_pages > MM_BUF_SIZE ) {
		p_hdr->n_too_large++;
		goto err;
	}

	spin_lock( &p_hdr->lock );

	p_list = &p_hdr->list[n_pages-1];
	if ( !IsListEmpty( &p_list->list_head ) ) {
		ASSERT( p_list->cnt > 0 );
		ASSERT( p_hdr->n_bufs > 0 );
		ptr = RemoveHeadList( &p_list->list_head );
		p_list->cnt--;
		p_hdr->n_bufs--;
		if ( IsListEmpty( &p_list->list_head ) )
			p_hdr->n_lists--;
	}
	
	spin_unlock( &p_hdr->lock );

err:
	return ptr;
}

boolean_t st_mm_free(PVOID ptr, ULONG size)
{
	boolean_t released = FALSE;
	mm_list_t *p_list;
	pmm_hdr_t p_hdr = &g_stat.drv.mm_hdr;
	ULONG n_pages = BYTES_TO_PAGES(size);

	ASSERT( n_pages > 0 );
	
	if ( n_pages > MM_BUF_SIZE ) {
		goto err;
	}

	if(p_hdr->enable_cache == FALSE) {
		goto err;
	}
	spin_lock( &p_hdr->lock );

	p_list = &p_hdr->list[n_pages-1];
	ASSERT( p_list->cnt >= 0 );

	if ( IsListEmpty( &p_list->list_head ) )
		p_hdr->n_lists++;
	InsertTailList( &p_list->list_head, (PLIST_ENTRY)ptr );
	p_list->cnt++;
	p_hdr->n_bufs++;
	released = TRUE;
	
	spin_unlock( &p_hdr->lock );

err:
	return released;
}

void st_mm_enable_cache(boolean_t val)
{
	g_stat.drv.mm_hdr.enable_cache = val;
}

void st_mm_report(char* title)
{
	int i;
	pmm_hdr_t p_hdr = &g_stat.drv.mm_hdr;

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"\n=====================================================================================\n"));

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"Memory Manager Report (%s): n_lists %d, n_bufs %d, n_too_large %d\n",
		title, p_hdr->n_lists, p_hdr->n_bufs, p_hdr->n_too_large ));

	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( 
		"=====================================================================================\n\n"));

	st_et_event("Memory Manager Report (%s): n_lists %d, n_bufs %d, n_too_large %d\n",
		title, p_hdr->n_lists, p_hdr->n_bufs, p_hdr->n_too_large );

	for ( i = 0; i < MM_BUF_SIZE; ++i ) {
		mm_list_t *p_list = &p_hdr->list[i];

		if ( !IsListEmpty( &p_list->list_head ) ) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( 
				"%04d page(s): cnt %d, \n",
				i+1, p_list->cnt ));
		}
	}
}
 
void st_mm_init()
{
	int i;
	pmm_hdr_t p_hdr = &g_stat.drv.mm_hdr;
	  
	spin_lock_init( &p_hdr->lock );
	p_hdr->n_bufs = 0;
	p_hdr->n_lists = 0;
	p_hdr->enable_cache = FALSE;
	p_hdr->n_too_large = 0;
	
	for ( i = 0; i < MM_BUF_SIZE; ++i ) {
		mm_list_t *p_list = &p_hdr->list[i];
		InitializeListHead( &p_list->list_head );
		p_list->cnt = 0;
	}
}
  
void st_mm_deinit()
{
	int i;
	void *ptr;
	pmm_hdr_t p_hdr = &g_stat.drv.mm_hdr;

	for ( i = 0; i < MM_BUF_SIZE; ++i ) {
		mm_list_t *p_list = &p_hdr->list[i];

		if ( !IsListEmpty( &p_list->list_head ) )
			p_hdr->n_lists--;
		while ( !IsListEmpty( &p_list->list_head ) ) {
			ASSERT( p_list->cnt > 0 );
			ptr = RemoveHeadList( &p_list->list_head );
			p_list->cnt--;
			p_hdr->n_bufs--;
			MmFreeContiguousMemory( ptr );
		}
		ASSERT( p_list->cnt == 0 );
	}
}

