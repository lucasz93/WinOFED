/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <windows.h>
#include <iba\ib_uvp.h>

#include <rdma\wvstatus.h>

static ib_api_status_t __stdcall
WvPreOpenCa(const ib_net64_t ca_guid, ci_umv_buf_t *p_umv_buf,
			ib_ca_handle_t *ph_uvp_ca)
{
	UNREFERENCED_PARAMETER(ca_guid);
	UNREFERENCED_PARAMETER(ph_uvp_ca);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static ib_api_status_t __stdcall
WvPostOpenCa(const ib_net64_t ca_guid, ib_api_status_t ioctl_status,
			 ib_ca_handle_t *ph_uvp_ca, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(ca_guid);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(ph_uvp_ca);
	UNREFERENCED_PARAMETER(p_umv_buf);
	return IB_SUCCESS;
}

static ib_api_status_t __stdcall
WvPreCloseCa(ib_ca_handle_t h_uvp_ca)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	return IB_SUCCESS;
}

static ib_api_status_t __stdcall
WvPostCloseCa(ib_ca_handle_t h_uvp_ca, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(ioctl_status);
	return IB_SUCCESS;
}

static ib_api_status_t __stdcall
WvPreAllocatePd(ib_ca_handle_t h_uvp_ca, ci_umv_buf_t *p_umv_buf,
				ib_pd_handle_t *ph_uvp_pd)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(ph_uvp_pd);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostAllocatePd(ib_ca_handle_t h_uvp_ca, ib_api_status_t ioctl_status,
				 ib_pd_handle_t *ph_uvp_pd, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(ph_uvp_pd);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDeallocatePd(const ib_pd_handle_t h_uvp_pd)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDeallocatePd(const ib_pd_handle_t h_uvp_pd, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ioctl_status);
}

static ib_api_status_t __stdcall
WvPreCreateAv(const ib_pd_handle_t h_uvp_pd, const ib_av_attr_t *p_addr_vector,
			  ci_umv_buf_t *p_umv_buf, ib_av_handle_t *ph_uvp_av)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(p_addr_vector);
	UNREFERENCED_PARAMETER(ph_uvp_av);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostCreateAv(const ib_pd_handle_t h_uvp_pd, ib_api_status_t ioctl_status,
			   ib_av_handle_t *ph_uvp_av, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(ph_uvp_av);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDestroyAv(const ib_av_handle_t h_uvp_av)
{
	UNREFERENCED_PARAMETER(h_uvp_av);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDestroyAv(const ib_av_handle_t h_uvp_av, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_av);
	UNREFERENCED_PARAMETER(ioctl_status);
}

static ib_api_status_t __stdcall
WvPreCreateSrq(const ib_pd_handle_t h_uvp_pd, const ib_srq_attr_t* const p_srq_attr,
			   ci_umv_buf_t *p_umv_buf, ib_srq_handle_t *ph_uvp_srq)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(p_srq_attr);
	UNREFERENCED_PARAMETER(ph_uvp_srq);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostCreateSrq(const ib_pd_handle_t h_uvp_pd, ib_api_status_t ioctl_status,
				ib_srq_handle_t *ph_uvp_srq, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(ph_uvp_srq);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreModifySrq(const ib_srq_handle_t h_uvp_srq, const ib_srq_attr_t * const p_srq_attr,
			   const ib_srq_attr_mask_t srq_attr_mask, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);
	UNREFERENCED_PARAMETER(p_srq_attr);
	UNREFERENCED_PARAMETER(srq_attr_mask);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostModifySrq(const ib_srq_handle_t h_uvp_srq, ib_api_status_t ioctl_status,
				ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreQuerySrq(ib_srq_handle_t h_uvp_srq, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostQuerySrq(ib_srq_handle_t h_uvp_srq, ib_api_status_t ioctl_status,
			   ib_srq_attr_t *p_query_attr, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(p_query_attr);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDestroySrq(const ib_srq_handle_t h_uvp_srq)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDestroySrq(const ib_srq_handle_t h_uvp_srq, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_srq);
	UNREFERENCED_PARAMETER(ioctl_status);
}

static ib_api_status_t __stdcall
WvPostCreateQp(const ib_pd_handle_t h_uvp_pd, ib_api_status_t ioctl_status,
			   ib_qp_handle_t *ph_uvp_qp, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(ph_uvp_qp);
	UNREFERENCED_PARAMETER(p_umv_buf);
	return IB_SUCCESS;
}

static ib_api_status_t __stdcall
WvPreModifyQp(const ib_qp_handle_t h_uvp_qp, const ib_qp_mod_t *p_modify_attr,
			  ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(p_modify_attr);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostModifyQp(const ib_qp_handle_t h_uvp_qp, ib_api_status_t ioctl_status,
			   ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreQueryQp(ib_qp_handle_t h_uvp_qp, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostQueryQp(ib_qp_handle_t h_uvp_qp, ib_api_status_t ioctl_status,
			  ib_qp_attr_t *p_query_attr, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(p_query_attr);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDestroyQp(const ib_qp_handle_t h_uvp_qp)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDestroyQp(const ib_qp_handle_t h_uvp_qp, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_qp);
	UNREFERENCED_PARAMETER(ioctl_status);
}

static ib_api_status_t __stdcall
WvPreCreateCq(const ib_ca_handle_t h_uvp_ca, uint32_t* const p_size,
			  ci_umv_buf_t *p_umv_buf, ib_cq_handle_t *ph_uvp_cq)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(p_size);
	UNREFERENCED_PARAMETER(ph_uvp_cq);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostCreateCq(const ib_ca_handle_t h_uvp_ca, ib_api_status_t ioctl_status,
			   const uint32_t size, ib_cq_handle_t *ph_uvp_cq, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_ca);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(size);
	UNREFERENCED_PARAMETER(ph_uvp_cq);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreResizeCq(const ib_cq_handle_t h_uvp_cq, uint32_t* const p_size,
			  ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_cq);
	UNREFERENCED_PARAMETER(p_size);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostResizeCq(const ib_cq_handle_t h_uvp_cq, ib_api_status_t ioctl_status,
			   const uint32_t size, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_cq);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(size);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDestroyCq(const ib_cq_handle_t h_uvp_cq)
{
	UNREFERENCED_PARAMETER(h_uvp_cq);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDestroyCq(const ib_cq_handle_t h_uvp_cq, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_cq);
	UNREFERENCED_PARAMETER(ioctl_status);
}

static ib_api_status_t __stdcall
WvPreCreateMw(const ib_pd_handle_t h_uvp_pd, ci_umv_buf_t *p_umv_buf,
			  ib_mw_handle_t *ph_uvp_mw)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ph_uvp_mw);

	RtlZeroMemory(p_umv_buf, sizeof(*p_umv_buf));
	return IB_SUCCESS;
}

static void __stdcall
WvPostCreateMw(const ib_pd_handle_t h_uvp_pd, ib_api_status_t ioctl_status,
			   net32_t rkey, ib_mw_handle_t *ph_uvp_mw, ci_umv_buf_t *p_umv_buf)
{
	UNREFERENCED_PARAMETER(h_uvp_pd);
	UNREFERENCED_PARAMETER(ioctl_status);
	UNREFERENCED_PARAMETER(rkey);
	UNREFERENCED_PARAMETER(ph_uvp_mw);
	UNREFERENCED_PARAMETER(p_umv_buf);
}

static ib_api_status_t __stdcall
WvPreDestroyMw(const ib_mw_handle_t h_uvp_mw)
{
	UNREFERENCED_PARAMETER(h_uvp_mw);
	return IB_SUCCESS;
}

static void __stdcall
WvPostDestroyMw(const ib_mw_handle_t h_uvp_mw, ib_api_status_t ioctl_status)
{
	UNREFERENCED_PARAMETER(h_uvp_mw);
	UNREFERENCED_PARAMETER(ioctl_status);
}

HRESULT WvGetUserVerbs(HMODULE hLib, uvp_interface_t *pVerbs)
{
	uvp_get_interface_t	pfgetif;
	ib_api_status_t		ib_status;

	pfgetif = (uvp_get_interface_t)GetProcAddress(hLib, "uvp_get_interface");
	if (pfgetif == NULL) {
		return GetLastError();
	}

	ib_status = pfgetif(IID_UVP, pVerbs);
	if (ib_status != IB_SUCCESS) {
		return WV_NOT_SUPPORTED;
	}

	// Provide default implementations
	if (pVerbs->pre_open_ca == NULL) {
		pVerbs->pre_open_ca = WvPreOpenCa;
	}
	if (pVerbs->post_open_ca == NULL) {
		pVerbs->post_open_ca = WvPostOpenCa;
	}
	if (pVerbs->pre_close_ca == NULL) {
		pVerbs->pre_close_ca = WvPreCloseCa;
	}
	if (pVerbs->post_close_ca == NULL) {
		pVerbs->post_close_ca = WvPostCloseCa;
	}
	
	if (pVerbs->pre_allocate_pd == NULL) {
		pVerbs->pre_allocate_pd = WvPreAllocatePd;
	}
	if (pVerbs->post_allocate_pd == NULL) {
		pVerbs->post_allocate_pd = WvPostAllocatePd;
	}
	if (pVerbs->pre_deallocate_pd == NULL) {
		pVerbs->pre_deallocate_pd = WvPreDeallocatePd;
	}
	if (pVerbs->post_deallocate_pd == NULL) {
		pVerbs->post_deallocate_pd = WvPostDeallocatePd;
	}

	if (pVerbs->pre_create_av == NULL) {
		pVerbs->pre_create_av = WvPreCreateAv;
	}
	if (pVerbs->post_create_av == NULL) {
		pVerbs->post_create_av = WvPostCreateAv;
	}
	if (pVerbs->pre_destroy_av == NULL) {
		pVerbs->pre_destroy_av = WvPreDestroyAv;
	}
	if (pVerbs->post_destroy_av == NULL) {
		pVerbs->post_destroy_av = WvPostDestroyAv;
	}
	
	if (pVerbs->pre_create_srq == NULL) {
		pVerbs->pre_create_srq = WvPreCreateSrq;
	}
	if (pVerbs->post_create_srq == NULL) {
		pVerbs->post_create_srq = WvPostCreateSrq;
	}
	if (pVerbs->pre_modify_srq == NULL) {
		pVerbs->pre_modify_srq = WvPreModifySrq;
	}
	if (pVerbs->post_modify_srq == NULL) {
		pVerbs->post_modify_srq = WvPostModifySrq;
	}
	if (pVerbs->pre_query_srq == NULL) {
		pVerbs->pre_query_srq = WvPreQuerySrq;
	}
	if (pVerbs->post_query_srq == NULL) {
		pVerbs->post_query_srq = WvPostQuerySrq;
	}
	if (pVerbs->pre_destroy_srq == NULL) {
		pVerbs->pre_destroy_srq = WvPreDestroySrq;
	}
	if (pVerbs->post_destroy_srq == NULL) {
		pVerbs->post_destroy_srq = WvPostDestroySrq;
	}

	if (pVerbs->post_create_qp == NULL) {
		pVerbs->post_create_qp = WvPostCreateQp;
	}
	if (pVerbs->pre_modify_qp == NULL) {
		pVerbs->pre_modify_qp = WvPreModifyQp;
	}
	if (pVerbs->post_modify_qp == NULL) {
		pVerbs->post_modify_qp = WvPostModifyQp;
	}
	if (pVerbs->pre_query_qp == NULL) {
		pVerbs->pre_query_qp = WvPreQueryQp;
	}
	if (pVerbs->post_query_qp == NULL) {
		pVerbs->post_query_qp = WvPostQueryQp;
	}
	if (pVerbs->pre_destroy_qp == NULL) {
		pVerbs->pre_destroy_qp = WvPreDestroyQp;
	}
	if (pVerbs->post_destroy_qp == NULL) {
		pVerbs->post_destroy_qp = WvPostDestroyQp;
	}

	if (pVerbs->pre_create_cq == NULL) {
		pVerbs->pre_create_cq = WvPreCreateCq;
	}
	if (pVerbs->post_create_cq == NULL) {
		pVerbs->post_create_cq = WvPostCreateCq;
	}
	if (pVerbs->pre_resize_cq == NULL) {
		pVerbs->pre_resize_cq = WvPreResizeCq;
	}
	if (pVerbs->post_resize_cq == NULL) {
		pVerbs->post_resize_cq = WvPostResizeCq;
	}
	if (pVerbs->pre_destroy_cq == NULL) {
		pVerbs->pre_destroy_cq = WvPreDestroyCq;
	}
	if (pVerbs->post_destroy_cq == NULL) {
		pVerbs->post_destroy_cq = WvPostDestroyCq;
	}

	if (pVerbs->pre_create_mw == NULL) {
		pVerbs->pre_create_mw = WvPreCreateMw;
	}
	if (pVerbs->post_create_mw == NULL) {
		pVerbs->post_create_mw = WvPostCreateMw;
	}
	if (pVerbs->pre_destroy_mw == NULL) {
		pVerbs->pre_destroy_mw = WvPreDestroyMw;
	}
	if (pVerbs->post_destroy_mw == NULL) {
		pVerbs->post_destroy_mw = WvPostDestroyMw;
	}

	return WV_SUCCESS;
}
