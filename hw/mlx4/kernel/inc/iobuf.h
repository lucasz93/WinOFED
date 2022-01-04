#pragma once

typedef struct {
	u64 va;  /* virtual address of the buffer */
	u64 size;     /* size in bytes of the buffer */
	LIST_ENTRY seg_que;
	u32 nr_pages;
	int seg_num;
	int is_cashed;
} iobuf_t;

/* iterator for getting segments of tpt */
typedef struct _iobuf_iter {
	void * seg_p;  /* the item from where to take the next translations */
	unsigned int pfn_ix; /* index from where to take the next translation */
} iobuf_iter_t;

void iobuf_deregister_with_cash(IN iobuf_t *iobuf_p);

void iobuf_deregister(IN iobuf_t *iobuf_p);

void iobuf_init(
	IN		u64 va,
	IN		u64 size,
	IN OUT	iobuf_t *iobuf_p);

int iobuf_register_with_cash(
	IN		u64 vaddr,
	IN		u64 size,
	IN		KPROCESSOR_MODE mode,
	IN OUT	enum ib_access_flags *acc_p,
	IN OUT	iobuf_t *iobuf_p);

void iobuf_iter_init(
	IN 		iobuf_t *iobuf_p, 
	IN OUT	iobuf_iter_t *iterator_p);

uint32_t iobuf_get_tpt_seg(
	IN		iobuf_t *iobuf_p, 
	IN OUT	iobuf_iter_t *iterator_p,
	IN		uint32_t n_pages_in, 
	IN OUT	uint64_t *page_tbl_p );


