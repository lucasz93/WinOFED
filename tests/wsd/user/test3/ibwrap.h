#include <iba/ib_al.h>

struct qp_pack {
	ib_al_handle_t al_handle;
	ib_ca_handle_t hca_handle;
	ib_pd_handle_t pd_handle;
	ib_qp_handle_t qp_handle;
	ib_cq_handle_t cq_handle;

	atomic32_t wq_posted;

	ib_ca_attr_t *ca_attr;
	ib_port_attr_t *hca_port;  /* Port to use */
	ib_qp_attr_t qp_attr;

	void (*comp)(struct qp_pack *, ib_wc_t *wc);
};

struct mr_pack {
	void *buf;
	size_t size;

	net32_t lkey;
    net32_t rkey;
TO_LONG_PTR(	ib_mr_handle_t , mr_handle) ; 
};


extern int create_mr(struct qp_pack *qp, struct mr_pack *mr, ib_access_t acl);
extern int delete_mr(struct mr_pack *mr);
extern int create_qp(struct qp_pack *qp);
extern int connect_qp(struct qp_pack *qp1, struct qp_pack *qp2);
extern int post_send_buffer(struct qp_pack *qp, struct mr_pack *local_mr, struct mr_pack *remote_mr, ib_wr_type_t opcode, int offset, size_t length);
extern int destroy_qp(struct qp_pack *qp);
extern int query_qp(struct qp_pack *qp);
int post_receive_buffer(struct qp_pack *qp, struct mr_pack *mr,
						int offset, size_t length);

#if 0
extern int move_qp_to_error(struct qp_pack *qp);
extern int move_qp_to_reset(struct qp_pack *qp);
extern int move_qp_to_drain(struct qp_pack *qp);
extern int control_qp_completion(struct qp_pack *qp, int enable);
#endif
