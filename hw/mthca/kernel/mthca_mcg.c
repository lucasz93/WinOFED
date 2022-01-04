/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_mcg.tmh"
#endif
#include "mthca_cmd.h"


struct mthca_mgm {
	__be32 next_gid_index;
	u32    reserved[3];
	u8     gid[16];
	__be32 qp[MTHCA_QP_PER_MGM];
};

static const u8 zero_gid[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/*
 * Caller must hold MCG table semaphore.  gid and mgm parameters must
 * be properly aligned for command interface.
 *
 *  Returns 0 unless a firmware command error occurs.
 *
 * If GID is found in MGM or MGM is empty, *index = *hash, *prev = -1
 * and *mgm holds MGM entry.
 *
 * if GID is found in AMGM, *index = index in AMGM, *prev = index of
 * previous entry in hash chain and *mgm holds AMGM entry.
 *
 * If no AMGM exists for given gid, *index = -1, *prev = index of last
 * entry in hash chain and *mgm holds end of hash chain.
 */
static int find_mgm(struct mthca_dev *dev,
		    u8 *gid, struct mthca_mailbox *mgm_mailbox,
		    u16 *hash, int *prev, int *index)
{
	struct mthca_mailbox *mailbox;
	struct mthca_mgm *mgm = mgm_mailbox->buf;
	u8 *mgid;
	int err;
	u8 status;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return -ENOMEM;
	mgid = mailbox->buf;

	memcpy(mgid, gid, 16);

	err = mthca_MGID_HASH(dev, mailbox, hash, &status);
	if (err)
		goto out;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("MGID_HASH returned status %02x\n", status));
		err = -EINVAL;
		goto out;
	}

		HCA_PRINT(TRACE_LEVEL_VERBOSE,HCA_DBG_LOW,("Hash for %04x:%04x:%04x:%04x:"
			  "%04x:%04x:%04x:%04x is %04x\n",
			  cl_ntoh16(((__be16 *) gid)[0]),
			  cl_ntoh16(((__be16 *) gid)[1]),
			  cl_ntoh16(((__be16 *) gid)[2]),
			  cl_ntoh16(((__be16 *) gid)[3]),
			  cl_ntoh16(((__be16 *) gid)[4]),
			  cl_ntoh16(((__be16 *) gid)[5]),
			  cl_ntoh16(((__be16 *) gid)[6]),
			  cl_ntoh16(((__be16 *) gid)[7]),
			  *hash));

	*index = *hash;
	*prev  = -1;

	do {
		err = mthca_READ_MGM(dev, *index, mgm_mailbox, &status);
		if (err)
			goto out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("READ_MGM returned status %02x\n", status));
			err =  -EINVAL;
			goto out;
		}

		if (!memcmp(mgm->gid, zero_gid, 16)) {
			if (*index != *hash) {
				HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("Found zero MGID in AMGM.\n"));
				err = -EINVAL;
			}
			goto out;
		}

		if (!memcmp(mgm->gid, gid, 16))
			goto out;

		*prev = *index;
		*index = cl_ntoh32(mgm->next_gid_index) >> 6;
	} while (*index);

	*index = -1;

 out:
	mthca_free_mailbox(dev, mailbox);
	return err;
}

int mthca_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_mailbox *mailbox;
	struct mthca_mgm *mgm;
	u16 hash;
	int index, prev;
	int link = 0;
	int i;
	int err;
	u8 status;

	UNREFERENCED_PARAMETER(lid);
	
	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mgm = mailbox->buf;

	down(&dev->mcg_table.mutex);

	err = find_mgm(dev, gid->raw, mailbox, &hash, &prev, &index);
	if (err)
		goto out;

	if (index != -1) {
		if (!memcmp(mgm->gid, zero_gid, 16))
			memcpy(mgm->gid, gid->raw, 16);
	} else {
		link = 1;

		index = mthca_alloc(&dev->mcg_table.alloc);
		if (index == -1) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("No AMGM entries left\n"));
			err = -ENOMEM;
			goto out;
		}

		err = mthca_READ_MGM(dev, index, mailbox, &status);
		if (err)
			goto out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("READ_MGM returned status %02x\n", status));
			err = -EINVAL;
			goto out;
		}

		memset(mgm, 0, sizeof *mgm);
		memcpy(mgm->gid, gid->raw, 16);
		mgm->next_gid_index = 0;
	}

	for (i = 0; i < MTHCA_QP_PER_MGM; ++i)
		if (mgm->qp[i] == cl_hton32(ibqp->qp_num | (1 << 31))) {
			HCA_PRINT(TRACE_LEVEL_VERBOSE,HCA_DBG_LOW,("QP %06x already a member of MGM\n", 
				  ibqp->qp_num));
			err = 0;
			goto out;
		} else if (!(mgm->qp[i] & cl_hton32(1UL << 31))) {
			mgm->qp[i] = cl_hton32(ibqp->qp_num | (1 << 31));
			break;
		}

	if (i == MTHCA_QP_PER_MGM) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("MGM at index %x is full.\n", index));
		err = -ENOMEM;
		goto out;
	}

	err = mthca_WRITE_MGM(dev, index, mailbox, &status);
	if (err)
		goto out;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("WRITE_MGM returned status %02x\n", status));
		err = -EINVAL;
		goto out;
	}

	if (!link)
		goto out;

	err = mthca_READ_MGM(dev, prev, mailbox, &status);
	if (err)
		goto out;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("READ_MGM returned status %02x\n", status));
		err = -EINVAL;
		goto out;
	}

	mgm->next_gid_index = cl_hton32(index << 6);

	err = mthca_WRITE_MGM(dev, prev, mailbox, &status);
	if (err)
		goto out;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("WRITE_MGM returned status %02x\n", status));
		err = -EINVAL;
	}

out:
	if (err && link && index != -1) {
		BUG_ON(index < dev->limits.num_mgms);
		mthca_free(&dev->mcg_table.alloc, index);
	}
	KeReleaseMutex(&dev->mcg_table.mutex,FALSE);
	mthca_free_mailbox(dev, mailbox);
	return err;
}

int mthca_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mthca_dev *dev = to_mdev(ibqp->device);
	struct mthca_mailbox *mailbox;
	struct mthca_mgm *mgm;
	u16 hash;
	int prev, index;
	int i, loc;
	int err;
	u8 status;

	UNREFERENCED_PARAMETER(lid);
	
	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mgm = mailbox->buf;

	down(&dev->mcg_table.mutex);

	err = find_mgm(dev, gid->raw, mailbox, &hash, &prev, &index);
	if (err)
		goto out;

	if (index == -1) {
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_LOW, ("MGID %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
			  "not found\n",
			  cl_ntoh16(((__be16 *) gid->raw)[0]),
			  cl_ntoh16(((__be16 *) gid->raw)[1]),
			  cl_ntoh16(((__be16 *) gid->raw)[2]),
			  cl_ntoh16(((__be16 *) gid->raw)[3]),
			  cl_ntoh16(((__be16 *) gid->raw)[4]),
			  cl_ntoh16(((__be16 *) gid->raw)[5]),
			  cl_ntoh16(((__be16 *) gid->raw)[6]),
			  cl_ntoh16(((__be16 *) gid->raw)[7])));
		err = -EINVAL;
		goto out;
	}

	for (loc = -1, i = 0; i < MTHCA_QP_PER_MGM; ++i) {
		if (mgm->qp[i] == cl_hton32(ibqp->qp_num | (1 << 31)))
			loc = i;
		if (!(mgm->qp[i] & cl_hton32(1UL << 31)))
			break;
	}

	if (loc == -1) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("QP %06x not found in MGM\n", ibqp->qp_num));
		err = -EINVAL;
		goto out;
	}

	mgm->qp[loc]   = mgm->qp[i - 1];
	mgm->qp[i - 1] = 0;

	err = mthca_WRITE_MGM(dev, index, mailbox, &status);
	if (err)
		goto out;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("WRITE_MGM returned status %02x\n", status));
		err = -EINVAL;
		goto out;
	}

	if (i != 1)
		goto out;

	if (prev == -1) {
		/* Remove entry from MGM */
		int amgm_index_to_free = cl_ntoh32(mgm->next_gid_index) >> 6;
		if (amgm_index_to_free) {
			err = mthca_READ_MGM(dev, amgm_index_to_free,
					     mailbox, &status);
			if (err)
				goto out;
			if (status) {
				HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("READ_MGM returned status %02x\n",
					  status));
				err = -EINVAL;
				goto out;
			}
		} else
			RtlZeroMemory(mgm->gid, 16);

		err = mthca_WRITE_MGM(dev, index, mailbox, &status);
		if (err)
			goto out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("WRITE_MGM returned status %02x\n", status));
			err = -EINVAL;
			goto out;
		}
		if (amgm_index_to_free) {
			BUG_ON(amgm_index_to_free < dev->limits.num_mgms);
			mthca_free(&dev->mcg_table.alloc, amgm_index_to_free);
		}
	} else {
		/* Remove entry from AMGM */
		int curr_next_index = cl_ntoh32(mgm->next_gid_index) >> 6;
		err = mthca_READ_MGM(dev, prev, mailbox, &status);
		if (err)
			goto out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("READ_MGM returned status %02x\n", status));
			err = -EINVAL;
			goto out;
		}

		mgm->next_gid_index = cl_hton32(curr_next_index << 6);

		err = mthca_WRITE_MGM(dev, prev, mailbox, &status);
		if (err)
			goto out;
		if (status) {
			HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("WRITE_MGM returned status %02x\n", status));
			err = -EINVAL;
			goto out;
		}
		BUG_ON(index < dev->limits.num_mgms);
		mthca_free(&dev->mcg_table.alloc, index);
	}

 out:
	KeReleaseMutex(&dev->mcg_table.mutex, FALSE);
	mthca_free_mailbox(dev, mailbox);
	return err;
}

int mthca_init_mcg_table(struct mthca_dev *dev)
{
	int err;
	int table_size = dev->limits.num_mgms + dev->limits.num_amgms;

	err = mthca_alloc_init(&dev->mcg_table.alloc,
		table_size,
		table_size - 1,
		dev->limits.num_mgms);

	if (err)
		return err;

	KeInitializeMutex(&dev->mcg_table.mutex,0);

	return 0;
}

void mthca_cleanup_mcg_table(struct mthca_dev *dev)
{
	mthca_alloc_cleanup(&dev->mcg_table.alloc);
}


