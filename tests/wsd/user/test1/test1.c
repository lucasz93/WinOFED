/* Tests the hardware management code and IP discovery */

#include "ibspdll.h"

struct ibspdll_globals g_ibsp;

#define ROUTING_SIZE 8
static struct
{
	const char *ip_source;
	const ib_net64_t guid;

	const char *ip_dest;

} routing[ROUTING_SIZE] = {
	{
	"10.2.1.1", CL_NTOH64(CL_CONST64(0x00066a00a0004037)), "10.2.1.3"}, {
	"10.2.1.2", CL_NTOH64(CL_CONST64(0x00066a01a0004037)), "10.2.1.3"}, {
	"10.2.1.3", CL_NTOH64(CL_CONST64(0x00066a00a000416b)), "10.2.1.1"}, {
	"10.2.1.4", CL_NTOH64(CL_CONST64(0x00066a01a000416b)), "10.2.1.1"}, {
	"10.3.1.1", CL_NTOH64(CL_CONST64(0x00066a01a000416b)), "10.3.1.3"}, {
	"10.3.1.2", CL_NTOH64(CL_CONST64(0x00066a01a000416b)), "10.3.1.1"}, {
	"10.3.1.3", CL_NTOH64(CL_CONST64(0x00066a01a000416b)), "10.3.1.1"}, {
	"10.3.1.4", CL_NTOH64(CL_CONST64(0x00066a01a000416b)), "10.3.1.3"}
};

int
do_test(void)
{
	int ret;
	cl_list_item_t *item_hca;
	cl_list_item_t *item_ip;
	struct ibsp_port *port_pr = NULL;
	struct in_addr ip_addr_pr;
	ib_net64_t remote_guid;
	struct in_addr remote_ip_addr;
	ib_path_rec_t path_rec;
	int i;

	/* Browse the tree and display it. */

	cl_spinlock_acquire(&g_ibsp.hca_mutex);

	for(item_hca = cl_qlist_head(&g_ibsp.hca_list);
		item_hca != cl_qlist_end(&g_ibsp.hca_list); item_hca = cl_qlist_next(item_hca)) {

		struct ibsp_hca *hca = PARENT_STRUCT(item_hca, struct ibsp_hca, item);
		cl_list_item_t *item_port;

		printf("Found HCA guid %I64x\n", CL_NTOH64(hca->guid));

		for(item_port = cl_qlist_head(&hca->ports_list);
			item_port != cl_qlist_end(&hca->ports_list);
			item_port = cl_qlist_next(item_port)) {

			struct ibsp_port *port = PARENT_STRUCT(item_port, struct ibsp_port, item);

			printf("  found port %d, guid %I64x\n", port->port_num,
				   CL_NTOH64(port->guid));

			for(item_ip = cl_qlist_head(&port->ip_list);
				item_ip != cl_qlist_end(&port->ip_list);
				item_ip = cl_qlist_next(item_ip)) {

				struct ibsp_ip_addr *ip =
					PARENT_STRUCT(item_ip, struct ibsp_ip_addr, item);

				printf("    %s\n", inet_ntoa(ip->ip_addr));

				/* Remember that for the PR test */
				port_pr = port;
				ip_addr_pr = ip->ip_addr;
			}
		}
	}

	cl_spinlock_release(&g_ibsp.hca_mutex);

	if (port_pr == NULL) {
		printf("BAD: port_pr is NULL\n");
		return 1;
	}

	/* Display the list of all IP addresses */
	printf("List of IP addresses:\n");

	cl_spinlock_acquire(&g_ibsp.ip_mutex);

	for(item_ip = cl_qlist_head(&g_ibsp.ip_list);
		item_ip != cl_qlist_end(&g_ibsp.ip_list); item_ip = cl_qlist_next(item_ip)) {

		struct ibsp_ip_addr *ip =
			PARENT_STRUCT(item_ip, struct ibsp_ip_addr, item_global);

		printf("    %s\n", inet_ntoa(ip->ip_addr));
	}

	cl_spinlock_release(&g_ibsp.ip_mutex);

	/* Query for the GUID of all local IP addresses */
	printf("Guid of local IP addresses:\n");
	for(item_ip = cl_qlist_head(&g_ibsp.ip_list);
		item_ip != cl_qlist_end(&g_ibsp.ip_list); item_ip = cl_qlist_next(item_ip)) {

		struct ibsp_ip_addr *ip =
			PARENT_STRUCT(item_ip, struct ibsp_ip_addr, item_global);

		ret = query_guid_address(port_pr, ip->ip_addr.S_un.S_addr, &remote_guid);
		if (ret) {
			printf("query_guid_address failed\n");
			return 1;
		}

		printf("got GUID %I64x for IP %s\n", CL_NTOH64(remote_guid),
			   inet_ntoa(ip->ip_addr));
	}

	/* Query for the GUID of all IP addresses */
	printf("Guid of IP addresses:\n");
	for(i = 0; i < ROUTING_SIZE; i++) {
		struct in_addr in;
		ib_net32_t ip_addr;

		ip_addr = inet_addr(routing[i].ip_source);
		in.S_un.S_addr = ip_addr;

		ret = query_guid_address(port_pr, ip_addr, &remote_guid);
		if (ret) {
			printf("query_guid_address failed for IP %s\n", inet_ntoa(in));
		} else {
			in.S_un.S_addr = ip_addr;
			printf("got GUID %I64x for IP %s\n", CL_NTOH64(remote_guid), inet_ntoa(in));
		}

		/* TODO: fill routing.guid */
	}

	/* Find the remote IP */
	remote_ip_addr.S_un.S_addr = INADDR_ANY;
	for(i = 0; i < ROUTING_SIZE; i++) {
		if (ip_addr_pr.S_un.S_addr == inet_addr(routing[i].ip_source)) {
			remote_ip_addr.S_un.S_addr = inet_addr(routing[i].ip_dest);
			break;
		}
	}
	if (remote_ip_addr.S_un.S_addr == INADDR_ANY) {
		/* Did not find source address. */
		printf("BAD- source IP %s not in routing\n", inet_ntoa(ip_addr_pr));
		return 1;
	}

	printf("going to test between %s", inet_ntoa(ip_addr_pr));
	printf(" and %s\n", inet_ntoa(remote_ip_addr));

	ret = query_guid_address(port_pr, remote_ip_addr.S_un.S_addr, &remote_guid);
	if (ret) {
		printf("query_guid_address failed for remote IP\n");
		return 1;
	}

	printf("querying PR between %I64x and %I64x\n",
		   CL_NTOH64(port_pr->guid), CL_NTOH64(remote_guid));

	ret = query_pr(port_pr, remote_guid, &path_rec);
	if (ret) {
		printf("query_pr failed\n");
		return 1;
	}
#if 0
	/* Stressing query PR */
	for(i = 0; i < 1000000; i++) {
		ret = query_pr(port_pr, remote_guid, &path_rec);
		if (ret) {
			printf("query_pr failed (at %d)\n", i);
			return 1;
		}
		if (i % 1000 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
#endif

#if 0
	while(1) {
		/* Display the list of all IP addresses */
		printf("List of IP addresses:\n");

		cl_spinlock_acquire(&g_ibsp.ip_mutex);

		for(item_ip = cl_qlist_head(&g_ibsp.ip_list);
			item_ip != cl_qlist_end(&g_ibsp.ip_list); item_ip = cl_qlist_next(item_ip)) {

			struct ibsp_ip_addr *ip =
				PARENT_STRUCT(item_ip, struct ibsp_ip_addr, item_global);

			printf("    %s\n", inet_ntoa(ip->ip_addr));
		}

		cl_spinlock_release(&g_ibsp.ip_mutex);

		Sleep(100);
	}
#endif

	return 0;
}

int
main(void)
{
	int ret;

	memset(&g_ibsp, 0, sizeof(g_ibsp));
	if (init_globals()) {
		CL_ERROR(IBSP_DBG_TEST, gdbg_lvl, ("init_globals failed\n"));
		return 1;
	}

	ret = ibsp_initialize();
	if (ret) {
		printf("ib_initialize failed (%d)\n", ret);
		return 1;
	}

	do_test();

	ib_release();

	release_globals();

	return 0;
}
