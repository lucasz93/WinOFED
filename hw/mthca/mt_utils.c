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

#include <mt_utils.h>

/* Nth element of the table contains the index of the first set bit of N; 8 - for N=0 */
char g_set_bit_tbl[256];

/* Nth element of the table contains the index of the first 0 bit of N; 8 - for N=255 */
char g_clr_bit_tbl[256];

void fill_bit_tbls()
{	
	unsigned long i;
	for (i=0; i<256; ++i) {
		g_set_bit_tbl[i] = (char)(_ffs_raw(&i,0) - 1);
		g_clr_bit_tbl[i] = (char)(_ffz_raw(&i,0) - 1);
	}
	g_set_bit_tbl[0] = g_clr_bit_tbl[255] = 8;
}


