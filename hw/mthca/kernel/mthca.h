#ifndef MTHCA_H
#define MTHCA_H

NTSTATUS mthca_init_one(hca_dev_ext_t *ext);
void mthca_remove_one(hca_dev_ext_t *ext);
int mthca_get_dev_info(struct mthca_dev *mdev, __be64 *node_guid, u32 *hw_id);

#endif

