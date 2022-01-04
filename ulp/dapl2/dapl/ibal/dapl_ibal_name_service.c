/*
 * Copyright (c) 2007-2008 Intel Corporation. All rights reserved.
 * Copyright (c) 2002, Network Appliance, Inc. All rights reserved. 
 * 
 * This Software is licensed under the terms of the "Common Public
 * License" a copy of which is in the file LICENSE.txt in the root
 * directory. The license is also available from the Open Source
 * Initiative, see http://www.opensource.org/licenses/cpl.php.
 *
 */

/**********************************************************************
 * 
 * MODULE: dapl_ibal_name_service.c
 *
 * PURPOSE: IP Name service
 *
 * $Id$
 *
 **********************************************************************/

#include "dapl.h"
#include "dapl_adapter_util.h"
#include "dapl_evd_util.h"
#include "dapl_cr_util.h"
#include "dapl_sp_util.h"
#include "dapl_ep_util.h"
#include "dapl_ia_util.h"
#include "dapl_ibal_util.h"
#include "dapl_name_service.h"
#include "dapl_name_service.h"
#include "dapl_ibal_name_service.h"
#include "iba\ibat.h"

#define IB_INFINITE_SERVICE_LEASE   0xFFFFFFFF
#define  DAPL_ATS_SERVICE_ID        ATS_SERVICE_ID //0x10000CE100415453
#define  DAPL_ATS_NAME              ATS_NAME
#define  HCA_IPV6_ADDRESS_LENGTH    16

extern dapl_ibal_root_t dapl_ibal_root;

char *
dapli_get_ip_addr_str(DAT_SOCK_ADDR6 *ipa, OPTIONAL char *buf)
{
    int rval;
    static char lbuf[24];
    char *str = (buf ? buf : lbuf);
  
    rval = ((struct sockaddr_in *)ipa)->sin_addr.s_addr;

    sprintf(str, "%d.%d.%d.%d",
                      (rval >> 0) & 0xff,
                      (rval >> 8) & 0xff,
                      (rval >> 16) & 0xff,
                      (rval >> 24) & 0xff);
    return str;
}


char *
dapli_IPaddr_str(DAT_SOCK_ADDR *sa, OPTIONAL char *buf)
{
	int rc;
	static char lbuf[48];
	char *str = (buf ? buf : lbuf);
	DWORD sa_len, sa_buflen;

	sa_len = (sa->sa_family == AF_INET6
			? sizeof(struct sockaddr_in6)
			: sizeof(struct sockaddr_in));
	sa_buflen = sizeof(lbuf);
	rc = WSAAddressToString( sa, sa_len, NULL, str, &sa_buflen );
	if ( rc ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() ERR: WSAAddressToString %#x sa_len %d\n",
				__FUNCTION__, WSAGetLastError(), sa_len);
	}
	return str;
}


char *
dapli_gid_str( void *gid, OPTIONAL char *buf)
{
	static char lbuf[48];
	char *str = (buf ? buf : lbuf);
	DWORD buflen = sizeof(lbuf);

	return (char*) inet_ntop( AF_INET6, gid, str, buflen );
}


DAT_RETURN
dapli_IbatQueryPathRecordByIPaddr( IN  DAT_IA_ADDRESS_PTR  remote_ia_address,
		                   OUT ib_path_rec_t       **p_path_rec )
{
    DAT_RETURN dat_status = DAT_SUCCESS;
    SOCKET s;
    SOCKADDR_INET src;
    ib_path_rec_t *pr;
    IBAT_PATH_BLOB path_blob;
    int ret;
    HRESULT hr;
    DWORD len = sizeof(src);

    *p_path_rec = NULL;
    s = WSASocket( AF_INET, SOCK_STREAM, 0, NULL, 0, 0 );
    if( s == INVALID_SOCKET )
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR, "%s() WSASocket() failed?\n",__FUNCTION__);
        return DAT_ERROR(DAT_INVALID_PARAMETER, 0);
    }

    ret = WSAIoctl( s,
                    SIO_ROUTING_INTERFACE_QUERY,
                    remote_ia_address,
                    sizeof(struct sockaddr_in),
                    &src,
                    len,
                    &len,
                    NULL,
                    NULL );
    closesocket( s );
    if( ret == SOCKET_ERROR )
    {
	dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
		"%s() WSAIoctl(SIO_ROUTING_INTERFACE_QUERY) failed?\n",__FUNCTION__);
        return DAT_ERROR(DAT_INVALID_PARAMETER, 0);
    }

    dapl_dbg_log ( DAPL_DBG_TYPE_CM, "%s() to [%s]?\n",
        __FUNCTION__, dapli_IPaddr_str((DAT_SOCK_ADDR*)&src,NULL));

    hr = IbatQueryPath( (struct sockaddr*)&src,
                        remote_ia_address,
                        (IBAT_PATH_BLOB*)&path_blob );
    if (FAILED(hr))
    {
	dapl_dbg_log ( DAPL_DBG_TYPE_ERR,"%s() IbatQueryPath() failed?\n",
				__FUNCTION__);
        dat_status = DAT_ERROR(DAT_INVALID_PARAMETER, 0);
    }
    else
    {
        pr  = (ib_path_rec_t*) dapl_os_alloc( sizeof(ib_path_rec_t) );
        if ( !pr )
            dat_status = DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_MEMORY);
        else
        {
            dapl_os_memzero( (void*)pr, sizeof(ib_path_rec_t) );
            dapl_os_memcpy( (void*) pr, (void*) &path_blob, sizeof(path_blob) );
            *p_path_rec = pr;
        }
    }

    return dat_status;
}


#ifndef NO_NAME_SERVICE

DAT_RETURN
dapls_ib_ns_map_gid (
        IN        DAPL_HCA                *hca_ptr,
        IN        DAT_IA_ADDRESS_PTR      p_ia_address,
        OUT       GID                     *p_gid)
{
    DAT_RETURN		dat_status;
    dapl_ibal_ca_t      *p_ca;
    dapl_ibal_port_t    *p_active_port;
    ib_path_rec_t	*path_rec;
    ib_api_status_t     ib_status;
    DAT_SOCK_ADDR6      ipv6_addr;

    p_ca = (dapl_ibal_ca_t *) hca_ptr->ib_hca_handle;

    if (NULL == p_ca)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
                       "--> DsNMG: There is no HCA = %d\n", __LINE__);
        return (DAT_INVALID_HANDLE);
    }

    /*
     * We are using the first active port in the list for
     * communication. We have to get back here when we decide to support
     * fail-over and high-availability.
     */
    p_active_port = dapli_ibal_get_port ( p_ca, (uint8_t)hca_ptr->port_num );

    if (NULL == p_active_port)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
                       "--> DsNMG: Port %d is not available = %d\n",
                       hca_ptr->port_num, __LINE__);
        return (DAT_INVALID_STATE);
    }

    if (p_active_port->p_attr->lid == 0) 
    {
            dapl_dbg_log (DAPL_DBG_TYPE_ERR, "--> DsNMG: Port %d has no LID "
                           "assigned; can not operate\n", 
                           p_active_port->p_attr->port_num);
            return (DAT_INVALID_STATE);
    }

    if (!dapl_os_memcmp (p_ia_address,
                         &hca_ptr->hca_address,
                         HCA_IPV6_ADDRESS_LENGTH))
    {
        /* We are operating in the LOOPBACK mode */
        p_gid->guid =p_active_port->p_attr->p_gid_table[0].unicast.interface_id;
        p_gid->gid_prefix =p_active_port->p_attr->p_gid_table[0].unicast.prefix;

        dapl_dbg_log (DAPL_DBG_TYPE_CM, "%s() LOOPBACK [%s] GID {%s}\n",
                __FUNCTION__, dapli_IPaddr_str((DAT_SOCK_ADDR *)p_ia_address, NULL ),
		dapli_gid_str((void*)&p_gid,NULL));

        return DAT_SUCCESS;
    }

    if (p_active_port->p_attr->link_state != IB_LINK_ACTIVE)
    {
        /* 
         * Port is DOWN; can not send or recv messages
         */
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
			"--> DsNMG: Port %d is DOWN; can not send to fabric\n", 
			p_active_port->p_attr->port_num);
        return (DAT_INVALID_STATE);
    }

    dapl_os_memzero (&ipv6_addr, sizeof (DAT_SOCK_ADDR6));

    if (p_ia_address->sa_family == AF_INET)
    {
        dapl_os_memcpy (&ipv6_addr.sin6_addr.s6_addr[12], 
                        &((struct sockaddr_in *)p_ia_address)->sin_addr.s_addr, 
                        4);
#if defined(DAPL_DBG) || 1 // XXX
        {
            char ipa[20];

            dapl_dbg_log (DAPL_DBG_TYPE_ERR/*XXX _CM*/, "--> DsNMG: Remote ia_address %s\n",
                      dapli_get_ip_addr_str((DAT_SOCK_ADDR6*)p_ia_address, NULL));
        }
#endif

    }
    else
    {
        /*
         * Assume IPv6 address
         */
        dapl_os_assert (p_ia_address->sa_family == AF_INET6);
        dapl_os_memcpy (ipv6_addr.sin6_addr.s6_addr,
                        ((DAT_SOCK_ADDR6 *)p_ia_address)->sin6_addr.s6_addr, 
                        HCA_IPV6_ADDRESS_LENGTH);
#if defined(DAPL_DBG) || 1 // XXX
        {
            int i;
            uint8_t *tmp = ipv6_addr.sin6_addr.s6_addr;

            dapl_dbg_log ( DAPL_DBG_TYPE_ERR/*XXX CM*/, 
                           "--> DsNMG: Remote ia_address -  ");

            for ( i = 1; i < HCA_IPV6_ADDRESS_LENGTH; i++)
            {
                dapl_dbg_log ( DAPL_DBG_TYPE_CM, "%x:", tmp[i-1] );
            }
            dapl_dbg_log ( DAPL_DBG_TYPE_CM, "%x\n", tmp[i-1] );
        }
#endif
    }

    dat_status = dapli_IbatQueryPathRecordByIPaddr( p_ia_address, &path_rec );
    if ( dat_status != DAT_SUCCESS )
    {
	dapl_dbg_log ( DAPL_DBG_TYPE_ERR,"%s() unable to resolve [%s]\n",
			 __FUNCTION__,dapli_IPaddr_str(p_ia_address,NULL));
    	return dat_status;
    }

    /* return the destination GID */ 
    p_gid->guid       = path_rec->dgid.unicast.interface_id;
    p_gid->gid_prefix = path_rec->dgid.unicast.prefix;

    dapl_os_free( (void*) path_rec, sizeof( ib_path_rec_t) );

    dapl_dbg_log (DAPL_DBG_TYPE_CM, "%s() [%s] @ GID {%s}\n",__FUNCTION__,
  		dapli_IPaddr_str((DAT_SOCK_ADDR *)p_ia_address, NULL ),
		dapli_gid_str((void*)&p_gid,NULL));

    return DAT_SUCCESS;
}



DAT_RETURN
dapls_ib_ns_map_ipaddr (
        IN        DAPL_HCA                *hca_ptr,
        IN        GID                     gid,
        OUT       DAT_IA_ADDRESS_PTR      p_ia_address)
{
    SOCKADDR_INET	ip_addr;
    dapl_ibal_ca_t      *p_ca;
    dapl_ibal_port_t    *p_active_port;
    ib_api_status_t     ib_status;
    HRESULT             hr;

    p_ca = (dapl_ibal_ca_t *) hca_ptr->ib_hca_handle;

    if (NULL == p_ca)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
                       "--> DsNMI: There is no HCA = %d\n", __LINE__);
        return (DAT_INVALID_HANDLE);
    }

    /*
     * We are using the first active port in the list for
     * communication. We have to get back here when we decide to support
     * fail-over and high-availability.
     */
    p_active_port = dapli_ibal_get_port ( p_ca, (uint8_t)hca_ptr->port_num );

    if (NULL == p_active_port)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
                       "--> DsNMI: Port %d is not available = %d\n",
                       hca_ptr->port_num, __LINE__);
        return (DAT_INVALID_STATE);
    }

    if (p_active_port->p_attr->lid == 0) 
    {
            dapl_dbg_log ( DAPL_DBG_TYPE_ERR, "--> DsNMI: Port %d has no LID "
                           "assigned; can not operate\n", 
                           p_active_port->p_attr->port_num);
            return (DAT_INVALID_STATE);
    }
    /*else 
    {
        // 
         // We are operating in the LOOPBACK mode
         //
        if ((gid.gid_prefix ==
             p_active_port->p_attr->p_gid_table[0].unicast.prefix) &&
            (gid.guid  == 
             p_active_port->p_attr->p_gid_table[0].unicast.interface_id))
        {
            dapl_os_memcpy (((DAT_SOCK_ADDR6 *)p_ia_address)->sin6_addr.s6_addr, 
                            hca_ptr->hca_address.sin6_addr.s6_addr,
                            HCA_IPV6_ADDRESS_LENGTH);
            return DAT_SUCCESS;
        }
        
    }*/

    if (p_active_port->p_attr->link_state != IB_LINK_ACTIVE)
    {
		/* 
         * Port is DOWN; can not send or recv messages
         */
	dapl_dbg_log ( DAPL_DBG_TYPE_ERR,"--> DsNMI: Port %d is DOWN; "
                               "can not send/recv to/from fabric\n", 
                               p_active_port->p_attr->port_num);
        return (DAT_INVALID_STATE);
    }

    hr = IbatQueryIPaddrByGid( (ib_gid_t*)&gid, (SOCKADDR_INET*)&ip_addr );

    if( FAILED(hr) )
    {
	dapl_dbg_log (DAPL_DBG_TYPE_ERR,
            "%s() unable to map GID {%s} hr %#x Sz %d\n",
		__FUNCTION__, dapli_gid_str((void*)&gid,NULL),hr,ip_addr.si_family);
        return DAT_INVALID_STATE;
    }
    assert(ip_addr.si_family == AF_INET || ip_addr.si_family == AF_INET6);

    /* ***********************
     * return the IP address
     *************************/ 
    dapl_os_memcpy ( (void *) p_ia_address,
                     (const void *)&ip_addr,
		     sizeof(*p_ia_address) );

   ((DAT_SOCK_ADDR6 *)p_ia_address)->sin6_family = AF_INET;

    dapl_dbg_log (DAPL_DBG_TYPE_CM, "%s() returns [%s]\n",__FUNCTION__,
        dapli_get_ip_addr_str((DAT_SOCK_ADDR6*) p_ia_address, NULL));
	
    return (DAT_SUCCESS);
}


/*
 * dapls_ib_ns_create_gid_map()
 *
 * Register a ServiceRecord containing uDAPL_svc_id, IP address and GID to SA
 * Other nodes can look it up by quering the SA
 *
 * Input:
 *        hca_ptr        HCA device pointer
 *
 * Output:
 *         none
 *
 * Returns:
 *         DAT_SUCCESS
 *         DAT_INVALID_PARAMETER
 */
DAT_RETURN
dapls_ib_ns_create_gid_map (
        IN        DAPL_HCA       *hca_ptr)
{
    UNUSED_PARAM( hca_ptr );
    return (DAT_SUCCESS);
}


DAT_RETURN
dapls_ib_ns_remove_gid_map (
        IN        DAPL_HCA       *hca_ptr)
{
    UNUSED_PARAM( hca_ptr );
    return (DAT_SUCCESS);
}

#endif /* NO_NAME_SERVICE */
