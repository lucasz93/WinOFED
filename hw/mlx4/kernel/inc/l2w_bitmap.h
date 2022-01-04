#pragma once

#define DECLARE_BITMAP(name,bits) \
	unsigned long name[BITS_TO_LONGS(bits)]

static inline unsigned long atomic_set_bit(int nr, volatile long * addr)
{
		return InterlockedOr( addr, (1 << nr) );
}

static inline unsigned long atomic_clear_bit(int nr, volatile long * addr)
{
	return InterlockedAnd( addr, ~(1 << nr) );
}

static inline  int set_bit(int nr,unsigned long * addr)
{
	addr += nr >> 5;
	return atomic_set_bit( nr & 0x1f, (volatile long *)addr );	 
}

static inline  int clear_bit(int nr, unsigned long * addr)
{
	addr += nr >> 5;
	return atomic_clear_bit( nr & 0x1f, (volatile long *)addr );  
}

static inline  int test_bit(int nr, const unsigned long * addr)
{
       int     mask;

       addr += nr >> 5;
       mask = 1 << (nr & 0x1f);
       return ((mask & *addr) != 0);
}

static inline void bitmap_zero(unsigned long *dst, int nbits)
{
	if (nbits <= BITS_PER_LONG)
		*dst = 0UL;
	else {
		int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
		RtlZeroMemory(dst, len);
	}
}

#define BITMAP_LAST_WORD_MASK(nbits)	 \
	( ((nbits) % BITS_PER_LONG) ? (1UL<<((nbits) % BITS_PER_LONG))-1 : ~0UL )

int __bitmap_full(const unsigned long *bitmap, int bits);

static inline int bitmap_full(const unsigned long *src, int nbits)
{
	if (nbits <= BITS_PER_LONG)
		return ! (~(*src) & BITMAP_LAST_WORD_MASK(nbits));
	else
	   	return __bitmap_full(src, nbits);
}

int __bitmap_empty(const unsigned long *bitmap, int bits);

static inline int bitmap_empty(const unsigned long *src, int nbits)
{
	if (nbits <= BITS_PER_LONG)
		return ! (*src & BITMAP_LAST_WORD_MASK(nbits));
	else
		return __bitmap_empty(src, nbits);
}

static inline void bitmap_fill(unsigned long *dst, int nbits)
{
	size_t nlongs = BITS_TO_LONGS(nbits);
	if (nlongs > 1) {
	int len = (int)((nlongs - 1) * sizeof(unsigned long));
	memset(dst, 0xff, len);
	}
	dst[nlongs - 1] = BITMAP_LAST_WORD_MASK(nbits);
}

