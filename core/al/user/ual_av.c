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
#include "al_pd.h"
#include "al_av.h"

#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_av.tmh"
#endif


static void
ual_get_gid_index(
	IN		struct _al_ci_ca					*p_ci_ca,
	IN		uint8_t								port_num,
	IN		ib_gid_t							*p_gid,
		OUT	uint8_t							*p_index)
{
	ib_port_attr_t		*p_port_attr;
	uint8_t			i;

	ci_ca_lock_attr( p_ci_ca );
	p_port_attr = &p_ci_ca->p_pnp_attr->p_port_attr[port_num-1];
	*p_index = 0;
	for( i = 0; i < p_port_attr->num_gids; i++ )
	{
		if( !cl_memcmp(p_gid, &p_port_attr->p_gid_table[i], sizeof(ib_gid_t)) )
		{
			*p_index = i;
			break;
		}
	}
	ci_ca_unlock_attr( p_ci_ca );
}

ib_api_status_t
ual_create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
	IN	OUT			ib_av_handle_t				h_av )
{
	ual_create_av_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	uvp_interface_t			uvp_intf = h_pd->obj.p_ci_ca->verbs.user_verbs;
	ib_av_attr_t			av_attr;

	AL_ENTER( AL_DBG_AV );
	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );
	av_attr = *p_av_attr;

	/* Pre call to the UVP library */
	if( h_pd->h_ci_pd && uvp_intf.pre_create_av )
	{
		if( p_av_attr->grh_valid )
		{
			ual_get_gid_index(h_pd->obj.p_ci_ca, av_attr.port_num, 
								&av_attr.grh.src_gid, &av_attr.grh.resv2);
		}

		status = uvp_intf.pre_create_av( h_pd->h_ci_pd,
			&av_attr, &ioctl_buf.in.umv_buf, &h_av->h_ci_av );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_AV );
			return (status == IB_VERBS_PROCESSING_DONE) ? IB_SUCCESS : status;
		}
	}

	ioctl_buf.in.h_pd = h_pd->obj.hdl;
	ioctl_buf.in.attr = *p_av_attr;

	cl_status = do_al_dev_ioctl( UAL_CREATE_AV, 
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out), &bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
		status = IB_ERROR;
	else
	{
		status = ioctl_buf.out.status;
		if(status == IB_SUCCESS)
			h_av->obj.hdl = ioctl_buf.out.h_av;
	}
	
	/* Post uvp call */
	if( h_pd->h_ci_pd && uvp_intf.post_create_av )
	{
		uvp_intf.post_create_av( h_pd->h_ci_pd,
			status, &h_av->h_ci_av, &ioctl_buf.out.umv_buf);
	}

	AL_EXIT( AL_DBG_AV );
	return status;
}


/*
 * This call does not go to the uvp library.  The handle should be
 * always created in the kernel if it is an alias pd.
 */
ib_api_status_t
ual_pd_alias_create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
	IN	OUT			ib_av_handle_t				h_av )
{
	ual_create_av_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;

	AL_ENTER( AL_DBG_AV );
	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	ioctl_buf.in.h_pd = h_pd->obj.hdl;
	ioctl_buf.in.attr = *p_av_attr;

	cl_status = do_al_dev_ioctl( UAL_CREATE_AV,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CREATE_AV IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status =  IB_ERROR;
	}else
	{
		status = ioctl_buf.out.status;
		if(status == IB_SUCCESS)
			h_av->obj.hdl = ioctl_buf.out.h_av;
	}


	AL_EXIT( AL_DBG_AV );
	return status;
}


/*
 * The functions below can be used by both the alias_pd as well as real pd.
 * For alias_pd, there won't be a uvp_av_handle, as the create call doesn't
 * go to uvp.  Since there is no uvp_av_handle, the query, modify and
 * destroy calls also will not call uvp library.  So the rest of the
 * functions can be shared by both the real pd, and alias pd.
 */
ib_api_status_t
ual_destroy_av(
	IN			ib_av_handle_t				h_av )
{
	ual_destroy_av_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	uvp_interface_t			uvp_intf = h_av->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_AV );
	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	if( h_av->h_ci_av && uvp_intf.pre_destroy_av )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_destroy_av( h_av->h_ci_av );
		CL_ASSERT( (status == IB_SUCCESS) ||
			(status == IB_VERBS_PROCESSING_DONE) );
		if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_AV );
			return (status == IB_VERBS_PROCESSING_DONE) ? IB_SUCCESS : status;
		}
	}

	ioctl_buf.in.h_av = h_av->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_DESTROY_AV,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_AV IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else if( ioctl_buf.out.status != IB_SUCCESS )
	{
		CL_ASSERT( status == IB_SUCCESS );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_DESTROY_AV IOCTL status %s\n",
			ib_get_err_str(ioctl_buf.out.status)) );
		status = ioctl_buf.out.status;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	/* Call vendor's post call */
	if( h_av->h_ci_av && uvp_intf.post_destroy_av )
		uvp_intf.post_destroy_av( h_av->h_ci_av, status );

	AL_EXIT( AL_DBG_AV );
	return status;
}



ib_api_status_t
ual_modify_av(
	IN			ib_av_handle_t				h_av,
	IN	const	ib_av_attr_t* const			p_av_attr)
{
	ual_modify_av_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	ib_av_t*				p_av = (ib_av_t*) h_av;
	uvp_interface_t			uvp_intf = p_av->obj.p_ci_ca->verbs.user_verbs;
	ib_av_attr_t			av_attr;

	AL_ENTER( AL_DBG_AV );
	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );
	av_attr = *p_av_attr;

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( p_av->h_ci_av && uvp_intf.pre_modify_av )
	{
		if( p_av_attr->grh_valid )
		{
			ual_get_gid_index(p_av->obj.p_ci_ca, av_attr.port_num, 
								&av_attr.grh.src_gid, &av_attr.grh.resv2);
		}

		/* Pre call to the UVP library */
		status = uvp_intf.pre_modify_av( p_av->h_ci_av,
			&av_attr, &ioctl_buf.in.umv_buf );
		if( status == IB_VERBS_PROCESSING_DONE )
		{
			/* Modification is done in user mode. Issue the post call */
			if( uvp_intf.post_modify_av )
			{
				uvp_intf.post_modify_av(
					p_av->h_ci_av, IB_SUCCESS, &ioctl_buf.in.umv_buf );
			}

			AL_EXIT( AL_DBG_AV );
			return IB_SUCCESS;
		}
		else if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_AV );
			return status;
		}
	}

	ioctl_buf.in.h_av = p_av->obj.hdl;
	ioctl_buf.in.attr = *p_av_attr;

	cl_status = do_al_dev_ioctl( UAL_MODIFY_AV,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_MODIFY_AV IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	/* Post uvp call */
	if( h_av->h_ci_av && uvp_intf.post_modify_av )
	{
		uvp_intf.post_modify_av(
			p_av->h_ci_av, status, &ioctl_buf.out.umv_buf );
	}

	AL_EXIT( AL_DBG_AV );
	return status;
}


ib_api_status_t
ual_query_av(
	IN			ib_av_handle_t				h_av,
		OUT		ib_av_attr_t*	const		p_av_attr,
		OUT		ib_pd_handle_t*	const		ph_pd )
{
	ual_query_av_ioctl_t	ioctl_buf;
	uintn_t					bytes_ret;
	cl_status_t				cl_status = CL_ERROR;
	ib_api_status_t			status = IB_ERROR;
	uvp_interface_t			uvp_intf = h_av->obj.p_ci_ca->verbs.user_verbs;

	AL_ENTER( AL_DBG_AV );
	/* Clear the ioctl_buf */
	cl_memclr( &ioctl_buf, sizeof(ioctl_buf) );

	/* Call the uvp pre call if the vendor library provided a valid ca handle */
	if( h_av->h_ci_av && uvp_intf.pre_query_av )
	{
		/* Pre call to the UVP library */
		status = uvp_intf.pre_query_av(
			h_av->h_ci_av, &ioctl_buf.in.umv_buf );
		if( status == IB_VERBS_PROCESSING_DONE )
		{
			/* Query is done in user mode. Issue the post call */
			if( uvp_intf.post_query_av )
			{
				uvp_intf.post_query_av( h_av->h_ci_av,
					IB_SUCCESS, p_av_attr, ph_pd, &ioctl_buf.in.umv_buf );
			}
			AL_EXIT( AL_DBG_AV );
			return IB_SUCCESS;
		}
		else if( status != IB_SUCCESS )
		{
			AL_EXIT( AL_DBG_AV );
			return status;
		}
	}

	ioctl_buf.in.h_av = h_av->obj.hdl;

	cl_status = do_al_dev_ioctl( UAL_QUERY_AV,
		&ioctl_buf.in, sizeof(ioctl_buf.in),
		&ioctl_buf.out, sizeof(ioctl_buf.out),
		&bytes_ret );

	if( cl_status != CL_SUCCESS || bytes_ret != sizeof(ioctl_buf.out) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("UAL_QUERY_AV IOCTL returned %s\n", CL_STATUS_MSG(cl_status)) );
		status = IB_ERROR;
	}
	else
	{
		status = ioctl_buf.out.status;
	}

	/* Post uvp call */
	if( h_av->h_ci_av && uvp_intf.post_query_av )
	{
		uvp_intf.post_query_av( h_av->h_ci_av,
			status, &ioctl_buf.out.attr, ph_pd,
			&ioctl_buf.out.umv_buf );
	}

	if( status == IB_SUCCESS )
		*p_av_attr = ioctl_buf.out.attr;

	AL_EXIT( AL_DBG_AV );
	return status;
}

