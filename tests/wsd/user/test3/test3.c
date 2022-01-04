#include <stdio.h>

#include "ibwrap.h"

#define SR_BUFFER_SIZE 1576
#define RDMA_BUFFER 500000

struct qp_pack qp1, qp2;
struct mr_pack mr1_send, mr1_recv, mr2_send, mr2_recv, mr1_rdma, mr2_rdma;

void qp1_comp(struct qp_pack *qp, ib_wc_t *wc)
{
	printf("QP1 - completion error %d for op %d\n", wc->status, wc->wc_type);

	if (wc->status) goto done;

	if (wc->wc_type == IB_WC_RECV) {

		if (create_mr(qp, &mr1_rdma, RDMA_BUFFER)) {
			printf("Cannot create RDMA buffer\n");
			goto done;
		}

		if (post_send_buffer(qp, &mr1_rdma, &mr2_rdma, WR_RDMA_READ, 0, RDMA_BUFFER)) {
			printf("post_send_buffer failed1\n");
			goto done;
		}

		if (post_send_buffer(qp, &mr1_send, NULL, WR_SEND, 0, 40)) {
			printf("post_send_buffer failed1\n");
			goto done;
		}
	}

done:
	;
}

void qp2_comp(struct qp_pack *qp, ib_wc_t *wc)
{
	printf("QP2 - completion error %d for op %d\n", wc->status, wc->wc_type);
	
	if (wc->status) goto done;

	if (wc->wc_type == IB_WC_RECV) {
		delete_mr(&mr2_rdma);
	}

done:
	;
}

int main(void)
{
	int i;

	/* Create both QP and move them into the RTR state */
	if (create_qp(&qp1) || create_qp(&qp2)) {
		printf("Failed to create a QP\n");
		goto done;
	}
	qp1.comp = qp1_comp;
	qp2.comp = qp2_comp;

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
	mr1_send.size = mr1_recv.size = mr2_send.size = mr2_recv.size = SR_BUFFER_SIZE;
	if (create_mr(&qp1, &mr1_send, 5*SR_BUFFER_SIZE) || 
		create_mr(&qp1, &mr1_recv, 6*SR_BUFFER_SIZE) ||
		create_mr(&qp2, &mr2_send, 5*SR_BUFFER_SIZE) ||
		create_mr(&qp2, &mr2_recv, 6*SR_BUFFER_SIZE)) {
		printf("Cannot create RDMA buffers\n");
		goto done;
	}

	/* Post receives */
	for (i=0; i<6; i++) {
		if (post_receive_buffer(&qp1, &mr1_recv, i*SR_BUFFER_SIZE, SR_BUFFER_SIZE)) {
			printf("post_recv_buffer failed1\n");
			goto done;
		}
	}

	for (i=0; i<6; i++) {
		if (post_receive_buffer(&qp2, &mr2_recv, i*SR_BUFFER_SIZE, SR_BUFFER_SIZE)) {
			printf("post_recv_buffer failed1\n");
			goto done;
		}
	}

	if (create_mr(&qp2, &mr2_rdma, RDMA_BUFFER)) {
		printf("Cannot create RDMA buffer\n");
	}

	if (post_send_buffer(&qp2, &mr2_send, NULL, WR_SEND, 0, 40)) {
		printf("post_send_buffer failed1\n");
		goto done;
	}

#if 0
	/* Wait for both wr to complete. */
	while(qp1.wq_posted != 0) {
		Sleep(100);
	}
#endif

	Sleep(4000);

	if (destroy_qp(&qp1) || destroy_qp(&qp2)) {
		printf("cannot destroy QP\n");
	}

	printf("End of test\n");

 done:
	return 0;
}
