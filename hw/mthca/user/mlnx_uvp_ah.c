/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "mt_l2w.h"
#include "mlnx_uvp.h"
#include "mlnx_ual_data.h"
#include "mx_abi.h"

static struct mthca_ah_page *__add_page(
	struct mthca_pd *pd,  int page_size, int per_page)
{
	struct mthca_ah_page *page;
	int i;

	page = cl_malloc(sizeof *page + per_page * sizeof (int));
	if (!page)
		return NULL;

	if (posix_memalign(&page->buf, page_size, page_size)) {
		cl_free(page);
		return NULL;
	}

	page->use_cnt = 0;
	for (i = 0; i < per_page; ++i)
		page->free[i] = ~0;

	page->prev = NULL;
	page->next = pd->ah_list;
	pd->ah_list = page;
	if (page->next)
		page->next->prev = page;

	return page;
}

int mthca_alloc_av(struct mthca_pd *pd, struct ibv_ah_attr *attr,
		   struct mthca_ah *ah, struct ibv_create_ah_resp *resp)
{
	if (mthca_is_memfree(pd->ibv_pd.context)) {
		ah->av = cl_malloc(sizeof *ah->av);
		if (!ah->av)
			return -ENOMEM;
	} else {
		struct mthca_ah_page *page;
		int ps;
		int pp;
		int i, j;

		ps = g_page_size;
		pp = ps / (sizeof *ah->av * 8 * sizeof (int));

		WaitForSingleObject( pd->ah_mutex, INFINITE );
		for (page = pd->ah_list; page; page = page->next)
			if (page->use_cnt < ps / (int)(sizeof *ah->av))
				for (i = 0; i < pp; ++i)
					if (page->free[i])
						goto found;

		page = __add_page(pd, ps, pp);
		if (!page) {
			ReleaseMutex( pd->ah_mutex );
			return -ENOMEM;
		}
		ah->in_kernel = TRUE;

	found:
		++page->use_cnt;

		for (i = 0, j = -1; i < pp; ++i)
			if (page->free[i]) {
				j = ffs(page->free[i]);
				page->free[i] &= ~(1 << (j - 1));
				ah->av = (struct mthca_av *)((uint8_t*)page->buf +
					(i * 8 * sizeof (int) + (j - 1)) * sizeof *ah->av);
				break;
			}

		ah->page = page;

		ReleaseMutex( pd->ah_mutex );
	}

	memset(ah->av, 0, sizeof *ah->av);

	ah->av->port_pd = cl_hton32(pd->pdn | (attr->port_num << 24));
	ah->av->g_slid  = attr->src_path_bits;
	ah->av->dlid    = cl_hton16(attr->dlid);
	ah->av->msg_sr  = (3 << 4) | /* 2K message */
	attr->static_rate;
	ah->av->sl_tclass_flowlabel = cl_hton32(attr->sl << 28);
	if (attr->is_global) {
		ah->av->g_slid |= 0x80;
		/* XXX get gid_table length */
		ah->av->gid_index = (attr->port_num - 1) * 32 +
			attr->grh.sgid_index;
		ah->av->hop_limit = attr->grh.hop_limit;
		ah->av->sl_tclass_flowlabel |=
			cl_hton32((attr->grh.traffic_class << 20) |
				    attr->grh.flow_label);
		memcpy(ah->av->dgid, attr->grh.dgid.raw, 16);
	} else {
		/* Arbel workaround -- low byte of GID must be 2 */
		ah->av->dgid[3] = cl_hton32(2);
	}
	return 0;
}

void mthca_free_av(struct mthca_ah *ah)
{
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)ah->h_uvp_pd;
	if (mthca_is_memfree(p_pd->ibv_pd->context)) {
		cl_free(ah->av);
	} else {
		struct mthca_pd *pd = to_mpd(p_pd->ibv_pd);
		struct mthca_ah_page *page;
		int i;

		WaitForSingleObject( pd->ah_mutex, INFINITE );
		page = ah->page;
		i = ((uint8_t *)ah->av - (uint8_t *)page->buf) / sizeof *ah->av;
		page->free[i / (8 * sizeof (int))] |= 1 << (i % (8 * sizeof (int)));
		--page->use_cnt;
		ReleaseMutex( pd->ah_mutex );
	}
}

//NB: temporary, for support of modify_qp
void mthca_set_av_params( struct mthca_ah *ah_p, struct ibv_ah_attr *ah_attr )
{
	struct mthca_av *av	 = ah_p->av;
	mlnx_ual_pd_info_t *p_pd = (mlnx_ual_pd_info_t *)ah_p->h_uvp_pd;
	struct mthca_pd *pd =to_mpd(p_pd->ibv_pd);

	// taken from mthca_alloc_av
	//TODO: why cl_hton32 ?
	av->port_pd = cl_hton32(pd->pdn | (ah_attr->port_num << 24));
	av->g_slid	= ah_attr->src_path_bits;
	//TODO: why cl_hton16 ?
	av->dlid		= cl_hton16(ah_attr->dlid);
	av->msg_sr	= (3 << 4) | /* 2K message */
		ah_attr->static_rate;
	//TODO: why cl_hton32 ?
	av->sl_tclass_flowlabel = cl_hton32(ah_attr->sl << 28);
	if (ah_attr->is_global) {
		av->g_slid |= 0x80;
		av->gid_index = (ah_attr->port_num - 1) * 32 +
			ah_attr->grh.sgid_index;
		av->hop_limit = ah_attr->grh.hop_limit;
		av->sl_tclass_flowlabel |= cl_hton32((ah_attr->grh.traffic_class << 20) |
			ah_attr->grh.flow_label);
		memcpy(av->dgid, ah_attr->grh.dgid.raw, 16);
	} else {
		/* Arbel workaround -- low byte of GID must be 2 */
		//TODO: why cl_hton32 ?
		av->dgid[3] = cl_hton32(2);
	}
}

