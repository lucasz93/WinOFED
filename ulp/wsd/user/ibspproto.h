/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

/* protos from socketinfo.c */
struct ibsp_socket_info *
create_socket_info(
	OUT				LPINT						lpErrno );

void AL_API
deref_socket_info(
	IN				struct ibsp_socket_info		*p_socket );

/* protos from extension.c */
HANDLE WSPAPI
IBSPRegisterMemory(
	IN				SOCKET						s,
	IN				PVOID						lpBuffer,
	IN				DWORD						dwBufferLength,
	IN				DWORD						dwFlags,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPDeregisterMemory(
	IN				SOCKET						s,
	IN				HANDLE						handle,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPRegisterRdmaMemory(
	IN				SOCKET						s,
	IN				PVOID						lpBuffer,
	IN				DWORD						dwBufferLength,
	IN				DWORD						dwFlags,
		OUT			LPVOID						lpRdmaBufferDescriptor,
	IN	OUT			LPDWORD						lpdwDescriptorLength,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPDeregisterRdmaMemory(
	IN				SOCKET						s,
	IN				LPVOID						lpRdmaBufferDescriptor,
	IN				DWORD						dwDescriptorLength,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPRdmaWrite(
	IN				SOCKET						s,
	IN				LPWSABUFEX					lpBuffers,
	IN				DWORD						dwBufferCount,
	IN				LPVOID						lpTargetBufferDescriptor,
	IN				DWORD						dwTargetDescriptorLength,
	IN				DWORD						dwTargetBufferOffset,
		OUT			LPDWORD						lpdwNumberOfBytesWritten,
	IN				DWORD						dwFlags,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPRdmaRead(
	IN				SOCKET						s,
	IN				LPWSABUFEX					lpBuffers,
	IN				DWORD						dwBufferCount,
	IN				LPVOID						lpTargetBufferDescriptor,
	IN				DWORD						dwTargetDescriptorLength,
	IN				DWORD						dwTargetBufferOffset,
		OUT			LPDWORD						lpdwNumberOfBytesRead,
	IN				DWORD						dwFlags,
	IN				LPWSAOVERLAPPED				lpOverlapped,
	IN				LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	IN				LPWSATHREADID				lpThreadId,
		OUT			LPINT						lpErrno );

int WSPAPI
IBSPMemoryRegistrationCacheCallback(
	IN				LPVOID						lpvAddress,
	IN				SIZE_T						Size,
		OUT			LPINT						lpErrno );

/* Protos from ibsp_iblow.c */
extern void
ib_release( void );

extern int
ibsp_initialize( void );

void
ib_release_cq_tinfo(
					struct cq_thread_info		*cq_tinfo );

void
ib_destroy_cq_tinfo(
					struct cq_thread_info		*cq_tinfo );

int
ib_create_socket(
	IN	OUT			struct ibsp_socket_info		*socket_info );

void
ib_destroy_socket(
	IN	OUT			struct ibsp_socket_info		*socket_info );

void
shutdown_and_destroy_socket_info(
	IN	OUT			struct ibsp_socket_info		*socket_info );

int
ib_cq_comp(
					void						*cq_context );

void
wait_cq_drain(
	IN	OUT			struct ibsp_socket_info		*socket_info );

void
ibsp_dup_overlap_abort(
	IN	OUT			struct ibsp_socket_info		*socket_info );

/* Protos from misc.c */
extern int
ibal_to_wsa_error(
	IN				const ib_api_status_t		status );

/* Protos from ibsp_ip.c */
int CL_API
ip_cmp(
	IN	const void* const		p_key1,
	IN	const void*	const		p_key2 );

int
query_guid_address(
	IN				const struct sockaddr		*p_src_addr,
	IN				const struct sockaddr		*p_dest_addr,
		OUT			ib_net64_t					*port_guid );

int
query_pr(
	IN				ib_net64_t					guid,
	IN				ib_net64_t					dest_port_guid,
	IN				uint16_t					dev_id,
	OUT			ib_path_rec_t					*path_rec );

int
build_ip_list(
	IN	OUT			LPSOCKET_ADDRESS_LIST		ip_list,
	IN	OUT			LPDWORD						ip_list_size,
		OUT			LPINT						lpErrno );

struct ibsp_port*
get_port_from_ip_address(
	IN		const	struct in_addr				sin_addr );

/* Protos from ibsp_cm.c */
extern int
ib_listen(
	IN				struct ibsp_socket_info		*socket_info );

void
ib_listen_cancel(
	IN				struct ibsp_socket_info		*socket_info );

void
ib_reject(
	IN		const	ib_cm_handle_t				h_cm,
	IN		const	ib_rej_status_t				rej_status );

int
ib_accept(
	IN				struct ibsp_socket_info		*socket_info,
	IN				ib_cm_req_rec_t				*cm_req_received );

int
ib_connect(
	IN				struct ibsp_socket_info		*socket_info,
	IN				ib_path_rec_t				*path_rec, 
	IN				ib_path_rec_t				*alt_path_rec );

void
ib_disconnect(
	IN				struct ibsp_socket_info		*socket_info,
	IN				struct disconnect_reason	*reason );

void
ib_listen_backlog(
	IN				struct ibsp_socket_info		*socket_info,
	IN				int							backlog );

/* ibsp_pnp.h */
ib_api_status_t
register_pnp( void );

void
unregister_pnp( void );

void
pnp_ca_remove(
					struct ibsp_hca				*hca);

/* ibsp_duplicate.c */
int
setup_duplicate_socket(
	IN				struct ibsp_socket_info		*socket_info,
	IN				HANDLE						h_dup_info );

int WSPAPI
IBSPDuplicateSocket(
					SOCKET						s,
					DWORD						dwProcessId,
					LPWSAPROTOCOL_INFOW			lpProtocolInfo,
					LPINT						lpErrno );

/* ibsp_mem.c */


struct memory_node *
lookup_partial_mr(
	IN				struct ibsp_socket_info		*s,
	IN				ib_access_t					acl_mask,
	IN				void						*start,
	IN				size_t						len );

struct memory_node *
ibsp_reg_mem(
	IN				struct ibsp_socket_info		*s,
	IN				ib_pd_handle_t				pd,
	IN				void						*start,
	IN				size_t						len,
	IN				ib_access_t					access_ctrl,
		OUT			LPINT						lpErrno );

int
ibsp_dereg_mem(
	IN				struct ibsp_socket_info		*s,
	IN				struct memory_node			*node,
		OUT			LPINT						lpErrno );

void
ibsp_dereg_hca(
	IN				struct mr_list				*mem_list );

void
ibsp_dereg_socket(
	IN				struct ibsp_socket_info		*s );

void
ibsp_hca_flush_mr_cache(
	IN				struct ibsp_hca				*p_hca,
	IN				LPVOID						lpvAddress,
	IN				SIZE_T						Size );

int
ibsp_conn_insert(
	IN				struct ibsp_socket_info		*socket_info );

void
ibsp_conn_remove(
	IN				struct ibsp_socket_info		*socket_info );

void
ibsp_post_select_event(
					struct ibsp_socket_info		*socket_info,
					int							event,
					int							error );

/* ibspdll.c */
extern int
init_globals( void );

extern void
release_globals( void );

inline ib_net64_t GetOtherPortGuid(ib_net64_t DestPortGuid)
{
	return DestPortGuid ^ 0x300000000000000;

}
void AL_API cm_apr_callback(
	IN				ib_cm_apr_rec_t				*p_cm_apr_rec );


void qp_event_handler(ib_async_event_rec_t *p_event);
