/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include "ual_support.h"
#include "al.h"
#include "al_ca.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_ca.tmh"
#endif

#include "ual_ca.h"
#include "ual_ci_ca.h"


void
close_vendor_lib(
	IN				verbs_interface_t			*p_vca_intf )
{
	if( p_vca_intf->h_uvp_lib )
		al_unload_uvp( p_vca_intf->h_uvp_lib );
}



ib_api_status_t
open_vendor_lib(
	IN		const	ib_net64_t					ca_guid,
	IN	OUT			verbs_interface_t			*p_vca_intf )
{
	ual_get_uvp_name_ioctl_t	al_ioctl;
	uintn_t						bytes_ret;
	cl_status_t					cl_status;
	void						*h_lib;
	uvp_get_interface_t			pfn_uvp_ifc;

	AL_ENTER( AL_DBG_CA );

	/* Initialize assuming no user-mode support */
	cl_memclr( &al_ioctl, sizeof(al_ioctl) );
	cl_memclr( p_vca_intf, sizeof(verbs_interface_t) );

	/* init with the guid */
	p_vca_intf->guid = ca_guid;

	al_ioctl.in.ca_guid = ca_guid;

	cl_status = do_al_dev_ioctl( UAL_GET_VENDOR_LIBCFG,
		&al_ioctl.in, sizeof(al_ioctl.in),
		&al_ioctl.out, sizeof(al_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(al_ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		return IB_ERROR;
	}

	if( !strlen( al_ioctl.out.uvp_lib_name ) )
	{
		/* Vendor does not implement user-mode library */
		AL_PRINT_EXIT(TRACE_LEVEL_WARNING ,AL_DBG_CA ,
			("No vendor lib for CA guid %I64x.\n", ca_guid) );
		return IB_UNSUPPORTED;
	}

	/*
	 * The vendor supports a user-mode library
	 * open the library and get the interfaces supported
	 */
	AL_PRINT(TRACE_LEVEL_INFORMATION ,AL_DBG_CA ,
		("Loading vendor lib (%s)\n", al_ioctl.out.uvp_lib_name) );
	h_lib = al_load_uvp( al_ioctl.out.uvp_lib_name );
	if (h_lib == NULL)
	{
#if defined( _DEBUG_ )
		al_uvp_lib_err( TRACE_LEVEL_WARNING,
			"!vendor lib (%s) not found for CA guid %"PRIx64".",
			al_ioctl.out.uvp_lib_name, ca_guid );
#endif
		AL_EXIT( AL_DBG_CA );
		return IB_SUCCESS;
	}

	pfn_uvp_ifc = (uvp_get_interface_t)
				   GetProcAddress( h_lib, "uvp_get_interface" );
	if( !pfn_uvp_ifc )
	{
#if defined( _DEBUG_ )
		al_uvp_lib_err( TRACE_LEVEL_ERROR,
			"failed to get vendor lib interface (%s) "
			"for CA guid %"PRIx64" returned ",
			al_ioctl.out.uvp_lib_name, ca_guid );
#endif
		al_unload_uvp( h_lib );
		AL_EXIT( AL_DBG_CA );
		return IB_SUCCESS;
	}

	/* Query the vendor-supported user-mode functions */
	pfn_uvp_ifc( IID_UVP, &p_vca_intf->user_verbs );
	p_vca_intf->h_uvp_lib = h_lib;
	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}



ib_api_status_t
ual_open_ca(
	IN		const	ib_net64_t					ca_guid,
	IN	OUT			al_ci_ca_t* const			p_ci_ca )
{
	ual_open_ca_ioctl_t		ca_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	ib_api_status_t			uvp_status = IB_SUCCESS;
	uvp_interface_t			uvp_intf = p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CA );

	cl_memclr( &ca_ioctl, sizeof(ca_ioctl) );

	/* Pre call to the UVP library */
	if( uvp_intf.pre_open_ca )
	{
		status = uvp_intf.pre_open_ca( ca_guid, &ca_ioctl.in.umv_buf, &p_ci_ca->h_ci_ca );
		if( status != IB_SUCCESS )
		{
			CL_ASSERT( status != IB_VERBS_PROCESSING_DONE );
			AL_EXIT(AL_DBG_CA);
			return status;
		}
	}

	ca_ioctl.in.guid = ca_guid;
	ca_ioctl.in.context = (ULONG_PTR)p_ci_ca;

	cl_status = do_al_dev_ioctl( UAL_OPEN_CA,
		&ca_ioctl.in, sizeof(ca_ioctl.in), &ca_ioctl.out, sizeof(ca_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ca_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("UAL_OPEN_CA IOCTL returned %s\n",
			CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ca_ioctl.out.status;
		p_ci_ca->obj.hdl = ca_ioctl.out.h_ca;
	}

	/* Post uvp call */
	if( uvp_intf.post_open_ca )
	{
		uvp_status = uvp_intf.post_open_ca( ca_guid, status,
			&p_ci_ca->h_ci_ca, &ca_ioctl.out.umv_buf );
	}

	if( (status == IB_SUCCESS) && (uvp_status != IB_SUCCESS) )
		status = uvp_status;

	AL_EXIT( AL_DBG_CA );
	return status;
}


ib_api_status_t
ual_close_ca(
	IN				al_ci_ca_t					*p_ci_ca )
{
	ual_close_ca_ioctl_t	ca_ioctl;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	uvp_interface_t			uvp_intf = p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CA );

	cl_memclr( &ca_ioctl, sizeof(ca_ioctl) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( p_ci_ca->h_ci_ca && uvp_intf.pre_close_ca )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_close_ca( p_ci_ca->h_ci_ca );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}

	ca_ioctl.in.h_ca = p_ci_ca->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_CLOSE_CA,
		&ca_ioctl.in, sizeof(ca_ioctl.in), &ca_ioctl.out, sizeof(ca_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ca_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CLOSE_CA IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ca_ioctl.out.status;
	}

	if( p_ci_ca->h_ci_ca && uvp_intf.post_close_ca )
		uvp_intf.post_close_ca( p_ci_ca->h_ci_ca, status );

	AL_EXIT( AL_DBG_CA );
	return status;
}



ib_api_status_t
ual_query_ca(
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size )
{
	/* Do we need to do any special checking here ?? */

	ual_query_ca_ioctl_t		ca_ioctl;
	uintn_t				bytes_ret;
	cl_status_t			cl_status = CL_SUCCESS;
	ib_api_status_t			status = IB_SUCCESS;
	uvp_interface_t			uvp_intf = h_ca->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CA );

	cl_memclr( &ca_ioctl, sizeof(ca_ioctl) );

	ca_ioctl.in.h_ca = h_ca->obj.p_ci_ca->obj.hdl;
	ca_ioctl.in.p_ca_attr = (ULONG_PTR)p_ca_attr;
	ca_ioctl.in.byte_cnt = *p_size;

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.pre_query_ca )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_query_ca( h_ca->obj.p_ci_ca->h_ci_ca,
			p_ca_attr, *p_size, &ca_ioctl.in.umv_buf );

		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}

	cl_status = do_al_dev_ioctl( UAL_QUERY_CA,
		&ca_ioctl.in, sizeof(ca_ioctl.in), &ca_ioctl.out, sizeof(ca_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ca_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_CA IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		*p_size = ca_ioctl.out.byte_cnt;
		status = ca_ioctl.out.status;
	}

	/* The attributes, if any, will be directly copied by proxy */
	/* Post uvp call */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.post_query_ca )
	{
		uvp_intf.post_query_ca( h_ca->obj.p_ci_ca->h_ci_ca,
			status, p_ca_attr, ca_ioctl.out.byte_cnt, &ca_ioctl.out.umv_buf );
	}

	AL_EXIT( AL_DBG_CA );
	return status;
}



ib_api_status_t
ual_modify_ca(
	IN		const ib_ca_handle_t				h_ca,
	IN		const uint8_t						port_num,
	IN		const ib_ca_mod_t					ca_mod,
	IN		const ib_port_attr_mod_t* const		p_port_attr_mod )
{
	/* Do we need to do any special checking here ?? */

	ual_modify_ca_ioctl_t		ca_ioctl;
	uintn_t						bytes_ret;
	cl_status_t					cl_status = CL_SUCCESS;
	ib_api_status_t				status = IB_SUCCESS;
	uvp_interface_t				uvp_intf = h_ca->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_CA );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.pre_modify_ca )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_modify_ca(
			h_ca->obj.p_ci_ca->h_ci_ca, port_num, ca_mod, p_port_attr_mod );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}

	ca_ioctl.in.h_ca = h_ca->obj.p_ci_ca->obj.hdl;
	ca_ioctl.in.port_num = port_num;
	ca_ioctl.in.ca_mod = ca_mod;
	ca_ioctl.in.port_attr_mod = *p_port_attr_mod;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_CA,
		&ca_ioctl.in, sizeof(ca_ioctl.in), &ca_ioctl.out, sizeof(ca_ioctl.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ca_ioctl.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MODIFY_CA IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ca_ioctl.out.status;
	}

	/* Post uvp call */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.post_modify_ca )
		uvp_intf.post_modify_ca( h_ca->obj.p_ci_ca->h_ci_ca, status );

	AL_EXIT( AL_DBG_CA );
	return status;
}



static ib_api_status_t
__convert_to_proxy_handles(
	IN				uint64_t*			const	dst_handle_array,
	IN		const	void**				const	src_handle_array,
	IN				uint32_t					num_handles )
{
	uint32_t		i;
	al_obj_t		*p_al_obj;

	for( i = 0; i < num_handles; i++ )
	{
		p_al_obj = (al_obj_t*)src_handle_array[i];
		if( (p_al_obj->type != AL_OBJ_TYPE_H_PD) &&
			(p_al_obj->type != AL_OBJ_TYPE_H_CQ) &&
			(p_al_obj->type != AL_OBJ_TYPE_H_AV) &&
			(p_al_obj->type != AL_OBJ_TYPE_H_QP) &&
			(p_al_obj->type != AL_OBJ_TYPE_H_MR) &&
			(p_al_obj->type != AL_OBJ_TYPE_H_MW) )
		{
			return IB_INVALID_HANDLE;
		}

		dst_handle_array[i] = p_al_obj->hdl;
	}
	return IB_SUCCESS;
}



ib_api_status_t
ib_ci_call(
	IN				ib_ca_handle_t				h_ca,
	IN		const	void**				const	handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t*			const	p_ci_op )
{
	ual_ci_call_ioctl_t		*p_ca_ioctl;
	size_t					in_sz;
	uintn_t					bytes_ret;
	cl_status_t				cl_status;
	ib_api_status_t			status;
	uvp_interface_t			uvp_intf;
	void**					p_uvp_handle_array = NULL;

	AL_ENTER( AL_DBG_CA );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_ci_op )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	uvp_intf = h_ca->obj.p_ci_ca->verbs.user_verbs;

	in_sz = sizeof(ual_ci_call_ioctl_t);
	if( num_handles > 1 )
		in_sz += (sizeof(uint64_t) * (num_handles - 1));

	p_ca_ioctl = cl_zalloc( in_sz );
	if( !p_ca_ioctl )
	{
		AL_EXIT( AL_DBG_CA );
		return IB_INSUFFICIENT_MEMORY;
	}

	if( num_handles > 0 )
	{
		status = __convert_to_proxy_handles(
			p_ca_ioctl->in.handle_array, handle_array, num_handles );
		if( status != IB_SUCCESS )
		{
			cl_free( p_ca_ioctl );
			AL_EXIT( AL_DBG_CA );
			return status;
		}

		p_uvp_handle_array = cl_zalloc( sizeof(void*) * num_handles );
		if( !p_uvp_handle_array )
		{
			cl_free( p_ca_ioctl );
			AL_EXIT( AL_DBG_CA );
			return IB_INSUFFICIENT_MEMORY;
		}

		/* Convert the handle array */
		status = al_convert_to_ci_handles(
			p_uvp_handle_array, handle_array, num_handles );
		if( status != IB_SUCCESS )
		{
			cl_free( p_uvp_handle_array );
			cl_free( p_ca_ioctl );
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}

	/* Pre call to the UVP library */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.pre_ci_call )
	{
		status = uvp_intf.pre_ci_call(
			h_ca->obj.p_ci_ca->h_ci_ca, p_uvp_handle_array,
			num_handles, p_ci_op, &p_ca_ioctl->in.umv_buf );
		if( status != IB_SUCCESS )
		{
			cl_free( p_uvp_handle_array );
			cl_free( p_ca_ioctl );
			AL_EXIT( AL_DBG_CA );
			return status;
		}
	}

	p_ca_ioctl->in.h_ca = h_ca->obj.p_ci_ca->obj.hdl;
	p_ca_ioctl->in.num_handles = num_handles;
	p_ca_ioctl->in.ci_op = *p_ci_op;

	cl_status = do_al_dev_ioctl( UAL_CI_CALL,
		&p_ca_ioctl->in, sizeof(p_ca_ioctl->in),
		&p_ca_ioctl->out, sizeof(p_ca_ioctl->out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(p_ca_ioctl->out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_CI_CALL IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = p_ca_ioctl->out.status;
	}

	/* Post uvp call */
	if( h_ca->obj.p_ci_ca->h_ci_ca && uvp_intf.post_ci_call )
	{
		uvp_intf.post_ci_call(
			h_ca->obj.p_ci_ca->h_ci_ca,
			status, p_uvp_handle_array, num_handles, p_ci_op,
			&p_ca_ioctl->out.umv_buf );
	}

	if( num_handles > 0 )
		cl_free( p_uvp_handle_array );
	cl_free( p_ca_ioctl );

	AL_EXIT( AL_DBG_CA );
	return status;
}
