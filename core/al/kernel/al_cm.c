/*
 * Copyright (c) 2008 Intel Corporation.  All rights reserved.
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

#include <iba/ib_al_ifc.h>
#include <iba/ib_cm_ifc.h>
#include "al_cm_cep.h"
#include "al_mgr.h"
#include "al_proxy.h"
#include "al_cm_conn.h"
#include "al_cm_sidr.h"

typedef struct _iba_cm_id_priv
{
	iba_cm_id	id;
	KEVENT		destroy_event;

}	iba_cm_id_priv;

static iba_cm_id*
cm_alloc_id(NTSTATUS (*callback)(iba_cm_id *p_id, iba_cm_event *p_event),
			void *context)
{
	iba_cm_id_priv	*id;

    #pragma prefast(suppress: 6014, "The allocated memory is freed in function cm_free_id.");
	id = ExAllocatePoolWithTag(NonPagedPool, sizeof(iba_cm_id_priv), 'mcbi');
	if (id == NULL) {
		return NULL;
	}

	KeInitializeEvent(&id->destroy_event, NotificationEvent, FALSE);
	id->id.callback = callback;
	id->id.context = context;
	return &id->id;
}

static void
cm_free_id(iba_cm_id *id)
{
	ExFreePool(CONTAINING_RECORD(id, iba_cm_id_priv, id));
}

static void
cm_destroy_handler(void *context)
{
	iba_cm_id_priv	*id = context;
	KeSetEvent(&id->destroy_event, 0, FALSE);
}

static void
cm_cep_handler(const ib_al_handle_t h_al, const net32_t cid)
{
	void				*context;
	net32_t				new_cid;
	ib_mad_element_t	*mad;
	iba_cm_id			*id;
	iba_cm_event		event;
	NTSTATUS			status;

	while (al_cep_poll(h_al, cid, &context, &new_cid, &mad) == IB_SUCCESS) {

		id = (iba_cm_id *) context;
		kal_cep_format_event(h_al, id->cid, mad, &event);

		status = id->callback(id, &event);
		if (!NT_SUCCESS(status)) {
			kal_cep_config(h_al, new_cid, NULL, NULL, NULL);
			kal_cep_destroy(h_al, id->cid, status);
			cm_free_id(id);
		}
		ib_put_mad(mad);
	}
}

static void
cm_listen_handler(const ib_al_handle_t h_al, const net32_t cid)
{
	iba_cm_id		*id;
	iba_cm_event	event;

	id = (iba_cm_id *) kal_cep_get_context(h_al, cid, NULL, NULL);
	memset(&event, 0, sizeof event);
	event.type = iba_cm_req_received;
	id->callback(id, &event);
}

static NTSTATUS
cm_get_request_ex(iba_cm_id *p_listen_id,
				  NTSTATUS (*callback)(iba_cm_id *p_id, iba_cm_event *p_event),
				  void *context, iba_cm_id **pp_id, iba_cm_event *p_event)
{
	void				*dummy;
	net32_t				new_cid;
	ib_mad_element_t	*mad;
	ib_api_status_t		ib_status;
	NTSTATUS			status;

	ib_status = al_cep_poll(gh_al, p_listen_id->cid, &dummy, &new_cid, &mad);
	if (ib_status != IB_SUCCESS) {
		return STATUS_NO_MORE_ENTRIES;
	}

	*pp_id = cm_alloc_id(callback, context);
	if (*pp_id == NULL) {
		kal_cep_destroy(gh_al, new_cid, STATUS_NO_MORE_ENTRIES);
		status = STATUS_NO_MEMORY;
		goto out;
	}

	kal_cep_config(gh_al, new_cid, cm_cep_handler, *pp_id, cm_destroy_handler);
	(*pp_id)->cid = new_cid;
	kal_cep_format_event(gh_al, new_cid, mad, p_event);
	status = STATUS_SUCCESS;

out:
	ib_put_mad(mad);
	return status;
}

static NTSTATUS
cm_get_request(iba_cm_id *p_listen_id, iba_cm_id **pp_id, iba_cm_event *p_event)
{
	return cm_get_request_ex(p_listen_id, p_listen_id->callback, p_listen_id, pp_id, p_event);
}

static NTSTATUS
cm_create_id(NTSTATUS (*callback)(iba_cm_id *p_id, iba_cm_event *p_event),
			 void *context, iba_cm_id **pp_id)
{
	iba_cm_id		*id;
	ib_api_status_t	ib_status;

	id = cm_alloc_id(callback, context);
	if (id == NULL) {
		return STATUS_NO_MEMORY;
	}

	ib_status = kal_cep_alloc(gh_al, &id->cid);
	if (ib_status != IB_SUCCESS) {
		cm_free_id(id);
		return ib_to_ntstatus(ib_status);
	}

	kal_cep_config(gh_al, id->cid, cm_cep_handler, id, cm_destroy_handler);

	*pp_id = id;
	return STATUS_SUCCESS;
}

static void
cm_destroy_id(iba_cm_id *p_id)
{
	iba_cm_id_priv	*id;

	id = CONTAINING_RECORD(p_id, iba_cm_id_priv, id);
	kal_cep_destroy(gh_al, p_id->cid, STATUS_SUCCESS);
	KeWaitForSingleObject(&id->destroy_event, Executive, KernelMode, FALSE, NULL);
	cm_free_id(p_id);
}

static NTSTATUS
cm_listen(iba_cm_id *p_id, net64_t service_id, void *p_compare_buf,
		  uint8_t compare_len, uint8_t compare_offset)
{
	ib_cep_listen_t info;
	ib_api_status_t	ib_status;

	info.svc_id = service_id;
	info.port_guid = IB_ALL_PORTS;
	info.p_cmp_buf = p_compare_buf;
	info.cmp_len = compare_len;
	info.cmp_offset = compare_offset;
	
	kal_cep_config(gh_al, p_id->cid, cm_listen_handler, p_id, cm_destroy_handler);
	ib_status = al_cep_listen(gh_al, p_id->cid, &info);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_cancel_listen(iba_cm_id *p_id)
{
	return ib_to_ntstatus(kal_cep_cancel_listen(gh_al, p_id->cid));
}

static NTSTATUS
cm_send_req(iba_cm_id *p_id, iba_cm_req *p_req)
{
	ib_api_status_t ib_status;
	
	ib_status = kal_cep_pre_req(gh_al, p_id->cid, p_req, 0, NULL);
	if (ib_status != IB_SUCCESS) {
		return ib_to_ntstatus(ib_status);
	}

	ib_status = al_cep_send_req(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_rep(iba_cm_id *p_id, iba_cm_rep *p_rep)
{
	ib_api_status_t ib_status;

	ib_status = kal_cep_pre_rep(gh_al, p_id->cid, p_rep, 0, NULL);
	if (ib_status != IB_SUCCESS) {
		return ib_to_ntstatus(ib_status);
	}

	ib_status = al_cep_send_rep(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_rtu(iba_cm_id *p_id, const void *p_pdata, uint8_t pdata_len)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_rtu(gh_al, p_id->cid, p_pdata, pdata_len);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_dreq(iba_cm_id *p_id, const void *p_pdata, uint8_t pdata_len)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_dreq(gh_al, p_id->cid, p_pdata, pdata_len);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_drep(iba_cm_id *p_id, const void *p_pdata, uint8_t pdata_len)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_drep(gh_al, p_id->cid, p_pdata, pdata_len);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_rej(iba_cm_id *p_id, ib_rej_status_t status,
			const void *p_ari, uint8_t ari_len,
			const void *p_pdata, uint8_t pdata_len)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_rej(gh_al, p_id->cid, status, p_ari, ari_len,
						   p_pdata, pdata_len);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_mra(iba_cm_id *p_id, uint8_t service_timeout,
			const void *p_pdata, uint8_t pdata_len)
{
	ib_cm_mra_t		mra;
	ib_api_status_t ib_status;

	mra.svc_timeout = service_timeout;
	mra.p_mra_pdata = p_pdata;
	mra.mra_length = pdata_len;

	ib_status = al_cep_mra(gh_al, p_id->cid, &mra);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_lap(iba_cm_id *p_id, iba_cm_lap *p_lap)
{
	ib_cm_lap_t		lap;
	ib_api_status_t	ib_status;

	RtlZeroMemory(&lap, sizeof lap);
	lap.p_lap_pdata = p_lap->p_pdata;
	lap.lap_length = p_lap->pdata_len;
	lap.remote_resp_timeout = p_lap->remote_resp_timeout;
	lap.p_alt_path = p_lap->p_alt_path;

	ib_status = al_cep_lap(gh_al, p_id->cid, &lap);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_apr(iba_cm_id *p_id, iba_cm_apr *p_apr)
{
	ib_cm_apr_t		apr;
	ib_qp_mod_t		attr;
	ib_api_status_t	ib_status;

	RtlZeroMemory(&apr, sizeof apr);
	apr.p_apr_pdata = p_apr->p_pdata;
	apr.apr_length = p_apr->pdata_len;
	apr.apr_status = p_apr->status;
	apr.info_length = p_apr->info_length;
	apr.p_info = p_apr->p_info;

	ib_status = al_cep_pre_apr(gh_al, p_id->cid, &apr, &attr);
	if (ib_status != IB_SUCCESS) {
		return ib_to_ntstatus(ib_status);
	}

	ib_status = al_cep_send_apr(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_sidr_req(iba_cm_id *p_id, iba_cm_sidr_req *p_req)
{
	UNUSED_PARAM(p_id);
	UNUSED_PARAM(p_req);

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
cm_send_sidr_rep(iba_cm_id *p_id, iba_cm_sidr_rep *p_rep)
{
	UNUSED_PARAM(p_id);
	UNUSED_PARAM(p_rep);

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
cm_get_qp_attr(iba_cm_id *p_id, ib_qp_state_t state, ib_qp_mod_t *p_attr)
{
	ib_api_status_t	ib_status;

	switch (state) {
	case IB_QPS_INIT:
		ib_status = al_cep_get_init_attr(gh_al, p_id->cid, p_attr);
		break;
	case IB_QPS_RTR:
		ib_status = al_cep_get_rtr_attr(gh_al, p_id->cid, p_attr);
		break;
	case IB_QPS_RTS:
		ib_status = al_cep_get_rts_attr(gh_al, p_id->cid, p_attr);
		break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_migrate(iba_cm_id *p_id)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_migrate(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_establish(iba_cm_id *p_id)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_established(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_format_rep(iba_cm_id *p_id, iba_cm_rep *p_rep, ib_qp_mod_t *p_init)
{
	ib_api_status_t ib_status;

	ib_status = kal_cep_pre_rep(gh_al, p_id->cid, p_rep, 0, p_init);
	return ib_to_ntstatus(ib_status);
}

static NTSTATUS
cm_send_formatted_rep(iba_cm_id *p_id)
{
	ib_api_status_t ib_status;

	ib_status = al_cep_send_rep(gh_al, p_id->cid);
	return ib_to_ntstatus(ib_status);
}

void cm_get_interface(iba_cm_interface *p_ifc)
{
	p_ifc->create_id = cm_create_id;
	p_ifc->destroy_id = cm_destroy_id;
	p_ifc->listen = cm_listen;
	p_ifc->get_request = cm_get_request;
	p_ifc->send_req = cm_send_req;
	p_ifc->send_rep = cm_send_rep;
	p_ifc->send_rtu = cm_send_rtu;
	p_ifc->send_dreq = cm_send_dreq;
	p_ifc->send_drep = cm_send_drep;
	p_ifc->send_rej = cm_send_rej;
	p_ifc->send_mra = cm_send_mra;
	p_ifc->send_lap = cm_send_lap;
	p_ifc->send_apr = cm_send_apr;
	p_ifc->send_sidr_req = cm_send_sidr_req;
	p_ifc->send_sidr_rep = cm_send_sidr_rep;
	p_ifc->get_qp_attr = cm_get_qp_attr;
	p_ifc->migrate = cm_migrate;
	p_ifc->established = cm_establish;
	p_ifc->format_rep = cm_format_rep;
	p_ifc->send_formatted_rep = cm_send_formatted_rep;
	p_ifc->get_request_ex = cm_get_request_ex;
	p_ifc->cancel_listen = cm_cancel_listen;
}
