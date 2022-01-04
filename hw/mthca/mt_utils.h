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


#ifndef MT_UTILS_H
#define MT_UTILS_H

// Nth element of the table contains the index of the first set bit of N; 8 - for N=0
extern char g_set_bit_tbl[256];
// Nth element of the table contains the index of the first cleared bit of N; 8 - for N=0
extern char g_clr_bit_tbl[256];

// DECLARE_BITMAP
#define BITS_PER_LONG		32
#define BITS_TO_LONGS(bits) \
	 (((bits)+BITS_PER_LONG-1)/BITS_PER_LONG)

/*
* fls: find last bit set.
* returns: 0 - if not found or N+1, if found Nth bit
*/

static __inline int fls(int x)
{
       int r = 32;

       if (!x)
               return 0;
       if (!(x & 0xffff0000u)) {
               x <<= 16;
               r -= 16;
       }
       if (!(x & 0xff000000u)) {
               x <<= 8;
               r -= 8;
       }
       if (!(x & 0xf0000000u)) {
               x <<= 4;
               r -= 4;
       }
       if (!(x & 0xc0000000u)) {
               x <<= 2;
               r -= 2;
       }
       if (!(x & 0x80000000u)) {
               x <<= 1;
               r -= 1;
       }
       return r;
}

/**
* _ffs_raw - find the first one bit in a word
* @addr: The address to start the search at
* @offset: The bitnumber to start searching at
*
* returns: 0 - if not found or N+1, if found Nth bit
*/
static __inline int _ffs_raw(const unsigned long *addr, int offset)
{
	//TODO: not an effective code - is better in Assembler
	int mask;
	int rbc;
	int ix;
	if (!*addr) return 0;
	mask = 1 << offset;
	rbc = BITS_PER_LONG - offset;
	for (ix=0; ix<rbc; ix++, mask<<=1) {
		if (*addr & mask)
			return offset + ix + 1;
	}
	return 0;
}

// as previous with offset = 0
static __inline int _ffs(const unsigned long *addr)
{
	unsigned char *ptr = (unsigned char *)addr;
	if (!*addr) return 0;					// skip sero dword
	if (!*(short*)ptr) ptr += 2;				// get to the non-zero word
	if (!*(char*)ptr) ptr++;						// get to the non-zero byte
	return (int)(((ptr - (unsigned char *)addr ) << 3) + g_set_bit_tbl[*ptr] + 1);
}


#define ffs(val)	_ffs((const unsigned long *)&(val))

/**
* _ffz_raw - find the first zero bit in a word
* @addr: The address to start the search at
* @offset: The bitnumber to start searching at
*
* returns: 0 - if not found or N+1, if found Nth bit
*/
static __inline int _ffz_raw(const unsigned long *addr, int offset)
{
	//TODO: not an effective code - is better in Assembler
	int mask;
	int rbc;
	int ix;
	if (!~*addr) return 0;
	mask = 1 << offset;
	rbc = BITS_PER_LONG - offset;
	for (ix=0; ix<rbc; ix++, mask<<=1) {
		if (!(*addr & mask))
			return offset + ix + 1;
	}
	return 0;
}


// as previous with offset = 0
static __inline int _ffz(const unsigned long *addr)
{
	unsigned char *ptr = (unsigned char *)addr;
	if (!~*addr) return 0;					// skip sero dword
	if (!~*(short*)ptr) ptr += 2;				// get to the non-zero word
	if (!~*(char*)ptr) ptr++;						// get to the non-zero byte
	return (int)(((ptr - (unsigned char *)addr ) << 3) + g_clr_bit_tbl[*ptr] + 1);
}

#define ffz(val)	_ffz((const unsigned long *)&val)

// Function: 
// 	finds the first bit, set in the bitmap
// Parameters:
// 	ptr	- address of the bitmap
//	bits_size	- the size in bits
// Returns:
//	the index of the first bit set; 'bits_size' - when there is noone
// Notes:
//	presumes, that ptr is aligned on dword
//	presumes, that the map contains an integer number of dwords
//	on bits_size=0 will return 0, but its an illegal case
//
static __inline int find_first_bit(const unsigned long *addr, unsigned bits_size)
{
	unsigned char *ptr = (unsigned char *)addr; 		// bitmap start
	unsigned char *end_ptr = (unsigned char *)(addr + BITS_TO_LONGS(bits_size)); 	// bitmap end

	while (ptr<end_ptr) {
		if (!*(int*)ptr) { ptr += 4; continue; }	// skip zero dword
		if (!*(short*)ptr) ptr += 2;				// get to the non-zero word
		if (!*(char*)ptr) ptr++;						// get to the non-zero byte
		return (int)(((ptr - (unsigned char *)addr ) << 3) + g_set_bit_tbl[*ptr]);
	}
	return bits_size;
}

static __inline int find_first_zero_bit(const unsigned long *addr, unsigned bits_size)
{
	unsigned char *ptr = (unsigned char *)addr; 		// bitmap start
	unsigned char *end_ptr = (unsigned char *)(addr + BITS_TO_LONGS(bits_size)); 	// bitmap end

	while (ptr<end_ptr) {
		if (!~*(int*)ptr) { ptr += 4; continue; }	// skip dword w/o zero bits
		if (!~*(short*)ptr) ptr += 2;				// get to the word with zero bits
		if (!~*(char*)ptr) ptr++;						// get to the byte with zero bits
		return (int)(((ptr - (unsigned char *)addr ) << 3) + g_clr_bit_tbl[*ptr]);
	}
	return bits_size;
}


/**
* find_next_zero_bit - find the first zero bit in a memory region
* @addr: The address to base the search on
* @offset: The bitnumber to start searching at
* @bits_size: The maximum size to search
*
* Returns the bit-number of the first zero bit, not the number of the byte
* containing a bit. If not found - returns 'size'
*/
static __inline int find_next_zero_bit(const unsigned long *addr, int bits_size, int offset)
{	
	int res;
	int ix = offset & 31;
	int set = offset & ~31;
	const unsigned long *p = addr + (set >> 5);

	// search in the first word while we are in the middle
	if (ix) {
		res = _ffz_raw(p, ix);
		if (res)
			return set + res - 1;
		++p;
		set += BITS_PER_LONG;
	}

	// search the rest of the bitmap
	res = find_first_zero_bit(p, bits_size - (unsigned)(32 * (p - addr)));
	return res + set;
}

void fill_bit_tbls();

#endif

