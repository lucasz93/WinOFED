#include <stdio.h>

#include "ibwrap.h"

int main(void)
{
	struct qp_pack qp1, qp2;
	struct mr_pack mr1, mr2, mr3;

	while(1) {
		/* Create both QP and move them into the RTR state */
		if (create_qp(&qp1) || create_qp(&qp2)) {
			printf("Failed to create a QP\n");
			goto done;
		}

		/* Connect both QP */
		if (connect_qp(&qp1, &qp2)) {
			printf("Failed to connect QP1\n");
			goto done;
		}
		if (connect_qp(&qp2, &qp1)) {
			printf("Failed to connect QP2\n");
			goto done;
		}

		/* Create RDMA buffers */
		mr1.size = mr2.size = mr3.size = 500000;
		if (create_mr(&qp1, &mr1, IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE | IB_AC_RDMA_READ) || 
			create_mr(&qp1, &mr2, IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE | IB_AC_RDMA_READ) ||
			create_mr(&qp2, &mr3, IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE | IB_AC_RDMA_READ)) {
			printf("Cannot create RDMA buffers\n");
			goto done;
		}

		if (post_send_buffer(&qp1, &mr1, &mr3, WR_RDMA_WRITE, 1004, mr1.size-1004)) {
			printf("post_send_buffer failed1\n");
			goto done;
		}

		if (post_send_buffer(&qp1, &mr2, &mr3, WR_RDMA_WRITE, 1004, mr1.size-1004)) {
			printf("post_send_buffer failed1\n");
			goto done;
		}

		/* Wait for both wr to complete. */
		while(qp1.wq_posted != 0) {
			Sleep(100);
		}

		if (delete_mr(&mr1) || delete_mr(&mr2) || delete_mr(&mr3)) {
			printf("cannot destroy mr\n");
		}

		if (destroy_qp(&qp1) || destroy_qp(&qp2)) {
			printf("cannot destroy QP\n");
		}
	}

	printf("End of test\n");

 done:
	return 0;
}
