/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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

#include <iba/ib_al.h>
#include <iba/ioc_ifc.h>
#include <complib/cl_bus_ifc.h>
#include "vnic_adapter.h"

//#include "vnic_driver.h"

extern struct _vnic_globals 	g_vnic;

NDIS_STATUS
vnic_get_adapter_params(
	IN		NDIS_HANDLE				h_handle,
	OUT		vnic_params_t* const 	p_params );

static
NDIS_STATUS
_adapter_set_sg_size(
	IN		NDIS_HANDLE		h_handle,
	IN		ULONG			mtu_size );

NDIS_STATUS
vnic_get_adapter_interface(
	IN		NDIS_HANDLE			h_handle,
	IN		vnic_adapter_t		*p_adapter);

static
vnic_path_record_t*
__path_record_add(
	IN		vnic_adapter_t* const	p_adapter,
	IN		ib_path_rec_t* const	p_path_rec );

static 
vnic_path_record_t*
__path_record_remove(
	IN		vnic_adapter_t* const	p_adapter,
	IN		ib_path_rec_t* const	p_path_rec );

static BOOLEAN
__path_records_match(
	IN		ib_path_rec_t* const	p_path1,
	IN		ib_path_rec_t* const	p_path2 );

static
vnic_path_record_t *
__path_record_find( 
	IN		vnic_adapter_t* const	p_adapter,
	IN		ib_path_rec_t* const	p_path_rec );

static
vnic_path_record_t*
__path_record_get(
	IN		vnic_adapter_t* const	p_adapter );

static void
__path_records_cleanup(
	IN	vnic_adapter_t	*p_adapter );

static ib_api_status_t
_adapter_open_ca(
	IN		vnic_adapter_t* const	p_adapter );


static ib_api_status_t
_adapter_close_ca(
	IN		vnic_adapter_t* const	p_adapter );

static 
ib_api_status_t
_adapter_netpath_update(
	IN		vnic_adapter_t* const	p_adapter,
	IN		viport_t* const			p_viport,
	IN		vnic_path_record_t*		p_path );

static
ib_api_status_t
adapter_netpath_update_and_connect(
	IN		vnic_adapter_t*	const	p_adapter,
	IN		vnic_path_record_t		*p_path );

static inline uint8_t
_get_ioc_num_from_iocguid(
	IN		ib_net64_t		*p_iocguid );

static BOOLEAN
__sid_valid(
	IN		vnic_adapter_t		*p_adapter,
	IN		ib_net64_t			sid );

static void
__adapter_cleanup(
	IN	vnic_adapter_t		*p_adapter );

#if ( LBFO_ENABLED )

static NDIS_STATUS
__adapter_set_failover_primary(
	 IN		vnic_adapter_t*  const p_adapter,
	 IN		BOOLEAN			promote_secondary );

static NDIS_STATUS
__adapter_set_failover_secondary(
	 IN		vnic_adapter_t* const	p_adapter,
	 IN		NDIS_HANDLE				primary_handle );

static void
__adapter_add_to_failover_list(
	 IN		vnic_adapter_t* const	p_adapter,
	 IN		lbfo_state_t		failover_state );
static void
__adapter_remove_from_failover_list(
	 IN		vnic_adapter_t* const	p_adapter );

static
vnic_adapter_t*
__adapter_find_on_failover_list(
	IN		int				list_flag,
	IN		uint32_t		bundle_id );

#endif // LBFO_ENABLED


uint32_t _get_instance(ioc_ifc_data_t	*ifc_data)
{
	cl_qlist_t	*qlist;
	cl_list_item_t	*item;
	vnic_adapter_t	*p_adapter;
	uint32_t	instance = 1;

	qlist = &g_vnic.adapter_list;

	if (cl_qlist_count(qlist)) {

		item = cl_qlist_head(qlist);

		while(item != cl_qlist_end(qlist)) {
			p_adapter = PARENT_STRUCT(item, vnic_adapter_t, list_adapter);
			if (p_adapter->ifc_data.guid == ifc_data->guid) {
				instance += 2; // Right now we are considering one interface will consume two instance IDs
			}
			item = cl_qlist_next(item);
		}

	}

	return instance;

}

ib_api_status_t
vnic_create_adapter(
	IN		NDIS_HANDLE			h_handle,
	IN		NDIS_HANDLE			wrapper_config_context,
	OUT		vnic_adapter_t**	const pp_adapter)
{
	NDIS_STATUS			status;
	ib_api_status_t		ib_status;
	vnic_adapter_t		*p_adapter;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	status = NdisAllocateMemoryWithTag( &p_adapter, sizeof(vnic_adapter_t), 'pada');

	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT(VNIC_DBG_ERROR,("Failed to allocate adapter\n"));
		return IB_INSUFFICIENT_MEMORY;
	}

	NdisZeroMemory( p_adapter, sizeof(vnic_adapter_t) );

	NdisAllocateSpinLock( &p_adapter->lock );
	NdisAllocateSpinLock( &p_adapter->path_records_lock );
	NdisAllocateSpinLock( &p_adapter->pending_list_lock );
	NdisAllocateSpinLock( &p_adapter->cancel_list_lock );
	
	InitializeListHead( &p_adapter->send_pending_list );
	InitializeListHead( &p_adapter->cancel_send_list );

	cl_qlist_init( &p_adapter->path_records_list );

	status = 
		vnic_get_adapter_params( wrapper_config_context, &p_adapter->params );

	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			(" vnic_get_adapter_params failed with status %d\n", status));
		NdisFreeMemory( p_adapter, sizeof(vnic_adapter_t), 0 );
		return IB_INVALID_PARAMETER;
	}

	status = vnic_get_adapter_interface( h_handle, p_adapter );
	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("failed status %x\n", status ) );
		NdisFreeMemory( p_adapter, sizeof(vnic_adapter_t), 0 );
		return IB_INVALID_PARAMETER;
	}

	/*Open AL */
	ib_status = p_adapter->ifc.open_al( &p_adapter->h_al );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("ib_open_al returned %s\n", 
				p_adapter->ifc.get_err_str( ib_status )) );
		NdisFreeMemory( p_adapter, sizeof(vnic_adapter_t), 0 );
		return ib_status;
	}

	/* we will use these indexes later as an viport instance ID for connect request
	*  since target requires unique instance per connection per IOC.
	*/
	p_adapter->primaryPath.instance = _get_instance(&p_adapter->ifc_data);
	p_adapter->secondaryPath.instance = (p_adapter->primaryPath.instance) + 1;
	/* set adapter level params here */
	p_adapter->vlan_info = p_adapter->params.VlanInfo;

#if ( LBFO_ENABLED )

	p_adapter->failover.bundle_id = p_adapter->params.bundle_id;
	p_adapter->failover.fo_state = _ADAPTER_NOT_BUNDLED;

#endif

	p_adapter->h_handle = h_handle;
	*pp_adapter = p_adapter;

	// Insert in global list of adapters
	cl_qlist_insert_tail (&g_vnic.adapter_list, &p_adapter->list_adapter);
	VNIC_EXIT( VNIC_DBG_ADAPTER );
	return IB_SUCCESS;
}


void
vnic_destroy_adapter(
	 IN	 vnic_adapter_t		*p_adapter)
{
	ib_api_status_t	ib_status = IB_SUCCESS;
	ib_pnp_handle_t  h_pnp;
	ib_al_handle_t	 h_al;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

	if( ( h_pnp = InterlockedExchangePointer( (void *)&p_adapter->h_pnp, NULL ) ) != NULL )
	{
		ib_status =
			p_adapter->ifc.dereg_pnp( h_pnp, NULL );
	}

		/* should not receive new path notifications anymore, safe to cleanup */

	if( InterlockedCompareExchange( (volatile LONG *)&p_adapter->state,
				INIC_DEREGISTERING, INIC_REGISTERED ) == INIC_REGISTERED )
	{
		__adapter_cleanup( p_adapter );
	}

	_adapter_close_ca( p_adapter );

#if ( LBFO_ENABLED )

	__adapter_remove_from_failover_list( p_adapter );

#endif

	InterlockedExchange( (volatile LONG *)&p_adapter->state, INIC_UNINITIALIZED );
	if( ( h_al = InterlockedExchangePointer( (void *)&p_adapter->h_al, NULL ) ) != NULL )
	{
		ib_status = p_adapter->ifc.close_al( h_al );
		ASSERT( ib_status == IB_SUCCESS );
	}

	cl_qlist_remove_item(&g_vnic.adapter_list, &p_adapter->list_adapter);
	NdisFreeSpinLock( &p_adapter->lock );
	NdisFreeSpinLock( &p_adapter->path_records_lock );
	NdisFreeMemory( p_adapter, sizeof(vnic_adapter_t), 0 );
	p_adapter = NULL;

	VNIC_EXIT( VNIC_DBG_ADAPTER );
}

static ib_api_status_t
_adapter_open_ca(
		IN		vnic_adapter_t* const	p_adapter )
{
	ib_api_status_t		ib_status = IB_SUCCESS;
	ib_al_ifc_t			*p_ifc	= &p_adapter->ifc;
	uint32_t			attr_size;
	ib_ca_attr_t		*p_ca_attrs;
	uint32_t			num;
	uint64_t	start_addr	= 0;
	
	if( p_adapter->h_ca )
		return ib_status;

	ib_status = p_ifc->open_ca( p_adapter->h_al,
								p_adapter->ifc_data.ca_guid,
								NULL, //ib_asyncEvent,
								p_adapter,
								&p_adapter->h_ca );

	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Failed to open hca\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	ib_status = p_ifc->query_ca( p_adapter->h_ca, NULL , &attr_size );
	if( ib_status != IB_INSUFFICIENT_MEMORY )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("ib_query_ca failed status %s\n",
			p_ifc->get_err_str( ib_status )) );
		
		ib_status = IB_INSUFFICIENT_RESOURCES;
		goto ca_failure;
	}

	ASSERT( attr_size );

	p_ca_attrs = cl_zalloc( attr_size );
	if ( p_ca_attrs == NULL )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Allocate %d bytes failed for Channel adapter\n", attr_size ));

		ib_status = IB_INSUFFICIENT_MEMORY;
		goto ca_failure;
	}

	ib_status = p_ifc->query_ca( p_adapter->h_ca, p_ca_attrs , &attr_size );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("CA attributes query failed\n") );
		cl_free ( p_ca_attrs );
		goto ca_failure;
	}

	p_adapter->ca.numPorts = p_ca_attrs->num_ports;
	if( p_adapter->ca.numPorts > VNIC_CA_MAX_PORTS )
		p_adapter->ca.numPorts = VNIC_CA_MAX_PORTS;

	for( num = 0; num < p_adapter->ca.numPorts; num++ )
		p_adapter->ca.portGuids[num] = p_ca_attrs->p_port_attr[num].port_guid;

	p_adapter->ca.caGuid = p_adapter->ifc_data.ca_guid;

	cl_free ( p_ca_attrs );

	ib_status = p_adapter->ifc.alloc_pd( p_adapter->h_ca,
		IB_PDT_NORMAL, p_adapter, &p_adapter->ca.hPd );
	if ( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT ( VNIC_DBG_ERROR,
			("Alloc PD failed status %s(%d)\n",
			p_adapter->ifc.get_err_str(ib_status), ib_status ));
		goto ca_failure;
	}

	if ( ( ib_status = ibregion_physInit( p_adapter,
							 &p_adapter->ca.region,
							 p_adapter->ca.hPd,
							 &start_addr,
							 MAX_PHYS_MEMORY ) ) != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT ( VNIC_DBG_ERROR,
			("phys region init failed status %s(%d)\n",
			p_adapter->ifc.get_err_str(ib_status), ib_status ));
		p_adapter->ifc.dealloc_pd( p_adapter->ca.hPd, NULL );
		goto ca_failure;
	}

	return ib_status;

ca_failure:
	if( p_adapter->h_ca )
		p_ifc->close_ca( p_adapter->h_ca, NULL );

	return ib_status;
}

static BOOLEAN
_adapter_params_sanity_check(vnic_params_t *p_params)
{
	DEFAULT_PARAM( p_params->MaxAddressEntries, MAX_ADDRESS_ENTRIES );
	DEFAULT_PARAM( p_params->MinAddressEntries, MIN_ADDRESS_ENTRIES );

	DEFAULT_PARAM( p_params->ViportStatsInterval, VIPORT_STATS_INTERVAL );
	DEFAULT_PARAM( p_params->ViportHbInterval, VIPORT_HEARTBEAT_INTERVAL );
	DEFAULT_PARAM( p_params->ViportHbTimeout, VIPORT_HEARTBEAT_TIMEOUT );

	DEFAULT_PARAM( p_params->ControlRspTimeout, CONTROL_RSP_TIMEOUT );
	DEFAULT_PARAM( p_params->ControlReqRetryCount, CONTROL_REQ_RETRY_COUNT );

	DEFAULT_PARAM( p_params->RetryCount, RETRY_COUNT );
	DEFAULT_PARAM( p_params->MinRnrTimer, MIN_RNR_TIMER );

	DEFAULT_PARAM( p_params->MaxViportsPerNetpath, MAX_VIPORTS_PER_NETPATH );
	DEFAULT_PARAM( p_params->DefaultViportsPerNetpath, DEFAULT_VIPORTS_PER_NETPATH );

	DEFAULT_PARAM( p_params->DefaultPkey, DEFAULT_PKEY );
	DEFAULT_PARAM( p_params->NotifyBundleSz, NOTIFY_BUNDLE_SZ );
	DEFAULT_PARAM( p_params->DefaultNoPathTimeout, DEFAULT_NO_PATH_TIMEOUT );
	DEFAULT_PARAM( p_params->DefaultPrimaryConnectTimeout, DEFAULT_PRI_CON_TIMEOUT );
	DEFAULT_PARAM( p_params->DefaultPrimaryReconnectTimeout, DEFAULT_PRI_RECON_TIMEOUT );
	DEFAULT_PARAM( p_params->DefaultPrimarySwitchTimeout, DEFAULT_PRI_SWITCH_TIMEOUT );
	DEFAULT_PARAM( p_params->DefaultPreferPrimary, DEFAULT_PREFER_PRIMARY );

	U32_RANGE( p_params->MaxAddressEntries );
	U32_RANGE( p_params->MinAddressEntries );
	RANGE_CHECK( p_params->MinMtu, MIN_MTU, MAX_MTU );
	RANGE_CHECK( p_params->MaxMtu, MIN_MTU, MAX_MTU );
	U32_RANGE( p_params->HostRecvPoolEntries );
	U32_RANGE( p_params->MinHostPoolSz );
	U32_RANGE( p_params->MinEiocPoolSz );
	U32_RANGE( p_params->MaxEiocPoolSz );
	U32_ZERO_RANGE( p_params->MinHostKickTimeout );
	U32_ZERO_RANGE( p_params->MaxHostKickTimeout );
	U32_ZERO_RANGE( p_params->MinHostKickEntries );
	U32_ZERO_RANGE( p_params->MaxHostKickEntries );
	U32_ZERO_RANGE( p_params->MinHostKickBytes );
	U32_ZERO_RANGE( p_params->MaxHostKickBytes );
	U32_RANGE( p_params->MinHostUpdateSz );
	U32_RANGE( p_params->MaxHostUpdateSz );
	U32_RANGE( p_params->MinEiocUpdateSz );
	U32_RANGE( p_params->MaxEiocUpdateSz );
	U8_RANGE( p_params->NotifyBundleSz );
	U32_ZERO_RANGE( p_params->ViportStatsInterval );
	U32_ZERO_RANGE( p_params->ViportHbInterval );
	U32_ZERO_RANGE( p_params->ViportHbTimeout );
	U32_RANGE( p_params->ControlRspTimeout );
	U8_RANGE( p_params->ControlReqRetryCount );
	ZERO_RANGE_CHECK( p_params->RetryCount, 0, 7 );
	ZERO_RANGE_CHECK( p_params->MinRnrTimer, 0, 31 );
	U32_RANGE( p_params->DefaultViportsPerNetpath );
	U8_RANGE( p_params->MaxViportsPerNetpath );
	U16_ZERO_RANGE( p_params->DefaultPkey );
	U32_RANGE( p_params->DefaultNoPathTimeout );
	U32_RANGE( p_params->DefaultPrimaryConnectTimeout );
	U32_RANGE( p_params->DefaultPrimaryReconnectTimeout );
	U32_RANGE( p_params->DefaultPrimarySwitchTimeout );
	BOOLEAN_RANGE( p_params->DefaultPreferPrimary );
	BOOLEAN_RANGE( p_params->UseRxCsum );
	BOOLEAN_RANGE( p_params->UseTxCsum );

	LESS_THAN_OR_EQUAL( p_params->MinAddressEntries, p_params->MaxAddressEntries );
	LESS_THAN_OR_EQUAL( p_params->MinMtu, p_params->MaxMtu );
	LESS_THAN_OR_EQUAL( p_params->MinHostPoolSz, p_params->HostRecvPoolEntries );
	POWER_OF_2( p_params->HostRecvPoolEntries );
	POWER_OF_2( p_params->MinHostPoolSz );
	POWER_OF_2( p_params->NotifyBundleSz );
	LESS_THAN( p_params->NotifyBundleSz, p_params->MinEiocPoolSz );
	LESS_THAN_OR_EQUAL( p_params->MinEiocPoolSz, p_params->MaxEiocPoolSz );
	POWER_OF_2( p_params->MinEiocPoolSz );
	POWER_OF_2( p_params->MaxEiocPoolSz );
	LESS_THAN_OR_EQUAL( p_params->MinHostKickTimeout, p_params->MaxHostKickTimeout );
	LESS_THAN_OR_EQUAL( p_params->MinHostKickEntries, p_params->MaxHostKickEntries );
	LESS_THAN_OR_EQUAL( p_params->MinHostKickBytes, p_params->MaxHostKickBytes );
	LESS_THAN_OR_EQUAL( p_params->MinHostUpdateSz, p_params->MaxHostUpdateSz );
	POWER_OF_2( p_params->MinHostUpdateSz );
	POWER_OF_2( p_params->MaxHostUpdateSz );
	LESS_THAN( p_params->MinHostUpdateSz, p_params->MinHostPoolSz );
	LESS_THAN( p_params->MaxHostUpdateSz, p_params->HostRecvPoolEntries );
	LESS_THAN_OR_EQUAL( p_params->MinEiocUpdateSz, p_params->MaxEiocUpdateSz );
	POWER_OF_2( p_params->MinEiocUpdateSz );
	POWER_OF_2( p_params->MaxEiocUpdateSz );
	LESS_THAN( p_params->MinEiocUpdateSz, p_params->MinEiocPoolSz );
	LESS_THAN( p_params->MaxEiocUpdateSz, p_params->MaxEiocPoolSz );
	LESS_THAN_OR_EQUAL( p_params->DefaultViportsPerNetpath, p_params->MaxViportsPerNetpath );

	return TRUE;

}
NDIS_STATUS
vnic_get_adapter_params(
	IN			NDIS_HANDLE				wrapper_config_context,
	OUT			vnic_params_t* const	p_params )
{
	NDIS_STATUS						status;
	NDIS_HANDLE						h_config;
	NDIS_CONFIGURATION_PARAMETER	*p_reg_prm;
	NDIS_STRING						keyword;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	CL_ASSERT(p_params );

	/* prepare params for default initialization */
	cl_memset( p_params, 0xff, sizeof (vnic_params_t) );

	NdisOpenConfiguration( &status, &h_config, wrapper_config_context );
	if( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("NdisOpenConfiguration returned 0x%.8x\n", status) );
		return status;
	}

	status = NDIS_STATUS_FAILURE;
	p_reg_prm = NULL;

	RtlInitUnicodeString( &keyword, L"PayloadMtu" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	p_params->MinMtu = ( status != NDIS_STATUS_SUCCESS ) ? MIN_MTU:
						p_reg_prm->ParameterData.IntegerData;
	p_params->MaxMtu = MAX_MTU;

	RtlInitUnicodeString( &keyword, L"UseRxCsum" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	p_params->UseRxCsum = ( status != NDIS_STATUS_SUCCESS ) ?
		TRUE : ( p_reg_prm->ParameterData.IntegerData )? TRUE : FALSE;

	RtlInitUnicodeString( &keyword, L"UseTxCsum" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	/* turn it on by default, if not present */
	p_params->UseTxCsum = ( status != NDIS_STATUS_SUCCESS ) ?
		TRUE : ( p_reg_prm->ParameterData.IntegerData )? TRUE : FALSE;
	
	/* handle VLAN + Priority paramters */
	RtlInitUnicodeString( &keyword, L"VlanId" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	if( status == NDIS_STATUS_SUCCESS )
	{
		p_params->VlanInfo = 
			( p_reg_prm->ParameterData.IntegerData & 0x00000fff );

		RtlInitUnicodeString( &keyword, L"UserPriority" );
		NdisReadConfiguration(
			&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );
	
		p_params->VlanInfo |= ( status != NDIS_STATUS_SUCCESS ) ? 0 :
		( ( p_reg_prm->ParameterData.IntegerData & 0x00000007 ) << 13 );
	}
	else
	{
		p_params->VlanInfo = 0;
	}

	RtlInitUnicodeString( &keyword, L"SecondaryPath" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	/* disable, if not set. Save extra viport slot */
	p_params->SecondaryPath = ( status != NDIS_STATUS_SUCCESS ) ?
		FALSE : ( p_reg_prm->ParameterData.IntegerData )? TRUE : FALSE;

	RtlInitUnicodeString( &keyword, L"Heartbeat" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );

	p_params->ViportHbInterval = ( status != NDIS_STATUS_SUCCESS ) ?
		VIPORT_HEARTBEAT_INTERVAL : p_reg_prm->ParameterData.IntegerData;

#if ( LBFO_ENABLED )
	/* read Failover group Name/Number */

	RtlInitUnicodeString( &keyword, L"BundleId" );
	NdisReadConfiguration(
		&status, &p_reg_prm, h_config, &keyword, NdisParameterInteger );
	
	p_params->bundle_id = ( status != NDIS_STATUS_SUCCESS ) ? 0 :
	p_reg_prm->ParameterData.IntegerData;
#endif // LBFO_ENABLED

	NdisCloseConfiguration( h_config );
	
	/* initialize the rest of  adapter parameters with driver's global set */
	if( g_vnic.p_params != NULL )
	{
		/* get global parameters from service entry */
		p_params->MinHostPoolSz = g_vnic.p_params[INDEX_MIN_HOST_POOL_SZ].value;
		p_params->HostRecvPoolEntries = g_vnic.p_params[INDEX_HOST_RECV_POOL_ENTRIES].value;

		p_params->MinEiocPoolSz = g_vnic.p_params[INDEX_MIN_EIOC_POOL_SZ].value;
		p_params->MaxEiocPoolSz = g_vnic.p_params[INDEX_MAX_EIOC_POOL_SZ].value;

		p_params->MinHostKickTimeout = g_vnic.p_params[INDEX_MIN_HOST_KICK_TIMEOUT].value;
		p_params->MaxHostKickTimeout = g_vnic.p_params[INDEX_MAX_HOST_KICK_TIMEOUT].value;

		p_params->MinHostKickEntries = g_vnic.p_params[INDEX_MIN_HOST_KICK_ENTRIES].value;
		p_params->MaxHostKickEntries = g_vnic.p_params[INDEX_MAX_HOST_KICK_ENTRIES].value;

		p_params->MinHostKickBytes = g_vnic.p_params[INDEX_MIN_HOST_KICK_BYTES].value;
		p_params->MaxHostKickBytes = g_vnic.p_params[INDEX_MAX_HOST_KICK_BYTES].value;
	
		p_params->MinHostUpdateSz = g_vnic.p_params[INDEX_MIN_HOST_UPDATE_SZ].value;
		p_params->MaxHostUpdateSz = g_vnic.p_params[INDEX_MAX_HOST_UPDATE_SZ].value;

		p_params->MinEiocUpdateSz = g_vnic.p_params[INDEX_MIN_EIOC_UPDATE_SZ].value;
		p_params->MaxEiocUpdateSz = g_vnic.p_params[INDEX_MAX_EIOC_UPDATE_SZ].value;
		
		p_params->ViportHbTimeout = g_vnic.p_params[INDEX_HEARTBEAT_TIMEOUT].value;
	}
	else
	{
		/* set default constants */
		p_params->MinHostPoolSz = MIN_HOST_POOL_SZ;
		p_params->HostRecvPoolEntries = HOST_RECV_POOL_ENTRIES;
		p_params->MinEiocPoolSz = MIN_EIOC_POOL_SZ;
		p_params->MaxEiocPoolSz = MAX_EIOC_POOL_SZ;
		p_params->MinHostKickTimeout = MIN_HOST_KICK_TIMEOUT;
		p_params->MaxHostKickTimeout = MAX_HOST_KICK_TIMEOUT;
		p_params->MinHostKickEntries = MIN_HOST_KICK_ENTRIES;
		p_params->MaxHostKickEntries = MAX_HOST_KICK_ENTRIES;
		p_params->MinHostKickBytes = MIN_HOST_KICK_BYTES;
		p_params->MaxHostKickBytes = MAX_HOST_KICK_BYTES;
		p_params->MinHostUpdateSz = MIN_HOST_UPDATE_SZ;
		p_params->MaxHostUpdateSz = MAX_HOST_UPDATE_SZ;
		p_params->MinEiocUpdateSz = MIN_EIOC_UPDATE_SZ;
		p_params->MaxEiocUpdateSz = MAX_EIOC_UPDATE_SZ;
	}

	status = ( _adapter_params_sanity_check(p_params)?
				NDIS_STATUS_SUCCESS: NDIS_STATUS_FAILURE );

	VNIC_EXIT( VNIC_DBG_ADAPTER );
	return status;
}

ib_api_status_t
adapter_viport_allocate(
	IN			vnic_adapter_t*	const		p_adapter,
	IN OUT		viport_t**		const		pp_viport )
{
	viport_t	*p_viport;
	NDIS_STATUS	 status;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	NdisAcquireSpinLock( &p_adapter->lock );

	status = NdisAllocateMemoryWithTag( &p_viport, sizeof(viport_t), 'trop' );
	if( status != NDIS_STATUS_SUCCESS )
	{
		NdisReleaseSpinLock( &p_adapter->lock );
		VNIC_TRACE_EXIT(VNIC_DBG_ERROR,
				( "Failed allocating Viport structure\n" ));
		return IB_ERROR;
	}

	NdisZeroMemory( p_viport, sizeof(viport_t) );
	NdisAllocateSpinLock( &p_viport->lock );
	InitializeListHead( &p_viport->listPtrs );
	cl_event_init( &p_viport->sync_event, FALSE );
	
	p_viport->p_adapter = p_adapter;
	viport_config_defaults ( p_viport );

	control_construct( &p_viport->control, p_viport );
	data_construct( &p_viport->data, p_viport );

	p_viport->ioc_num = _get_ioc_num_from_iocguid( &p_adapter->ifc_data.guid );
	if( !p_adapter->ioc_num )
	{
		p_adapter->ioc_num = p_viport->ioc_num;
	}

	CL_ASSERT( p_adapter->ioc_num == p_viport->ioc_num );
	
	*pp_viport = p_viport;

	NdisReleaseSpinLock( &p_adapter->lock );

	VNIC_EXIT( VNIC_DBG_ADAPTER );
	return IB_SUCCESS;
}


static void
_adapter_netpath_free(
	IN		vnic_adapter_t* const	p_adapter,
	IN		Netpath_t*				p_netpath )
{

	UNREFERENCED_PARAMETER( p_adapter );

	if( netpath_is_valid( p_netpath ) )
	{
		VNIC_TRACE( VNIC_DBG_INFO,
					("IOC[%d] instance %d Free Path\n",
					p_adapter->ioc_num, p_netpath->instance ));

		netpath_free( p_netpath );
		netpath_init( p_netpath, NULL );
	}
}

NDIS_STATUS
adapter_set_mcast(
	IN				vnic_adapter_t* const		p_adapter,
	IN				mac_addr_t* const			p_mac_array,
	IN		const	uint8_t						mc_count )
{
	NDIS_STATUS	status;
	int i;
	VNIC_ENTER( VNIC_DBG_MCAST );

	if( !netpath_is_valid( p_adapter->p_currentPath ) )
		return NDIS_STATUS_NOT_ACCEPTED;

	VNIC_TRACE( VNIC_DBG_MCAST,
		("IOC[%d] MCAST COUNT to set = %d\n", 
						p_adapter->ioc_num, mc_count));

	/* Copy the MC address list into the adapter. */
	if( mc_count )
	{
		RtlCopyMemory(
			p_adapter->mcast_array, p_mac_array, mc_count * MAC_ADDR_LEN );
		for( i = 0; i < mc_count; i++ )
		VNIC_TRACE( VNIC_DBG_MCAST,
			("[%d] %02x:%02x:%02x:%02x:%02x:%02x\n", i,
			p_mac_array->addr[0], p_mac_array->addr[1],	p_mac_array->addr[2],
			p_mac_array->addr[3], p_mac_array->addr[4],	p_mac_array->addr[5] ));
	}
	p_adapter->mc_count = mc_count;

	p_adapter->pending_set = TRUE;
	status = netpath_setMulticast( p_adapter->p_currentPath );
	if( status != NDIS_STATUS_PENDING )
	{
		p_adapter->pending_set = FALSE;
	}

	VNIC_EXIT( VNIC_DBG_MCAST );
	return status;
}


static BOOLEAN
__path_records_match(
	IN			ib_path_rec_t* const	p_path1,
	IN			ib_path_rec_t* const	p_path2 )
{
	if( p_path1->dgid.unicast.prefix != p_path2->dgid.unicast.prefix )
		return FALSE;
	if( p_path1->dgid.unicast.interface_id != p_path2->dgid.unicast.interface_id )
		return FALSE;
	if( p_path1->dlid != p_path2->dlid )
		return FALSE;

	if( p_path1->sgid.unicast.prefix != p_path2->sgid.unicast.prefix )
		return FALSE;
	if( p_path1->sgid.unicast.interface_id != p_path2->sgid.unicast.interface_id )
		return FALSE;
	if( p_path1->slid != p_path2->slid )
		return FALSE;

	if( p_path1->pkey != p_path2->pkey )
		return FALSE;
	if( p_path1->rate != p_path2->rate )
		return FALSE;
	if( ib_path_rec_sl(p_path1) != ib_path_rec_sl(p_path2) )
		return FALSE;
	if( p_path1->tclass != p_path2->tclass )
		return FALSE;

	return TRUE;
}

static BOOLEAN
__sid_valid(
		IN		vnic_adapter_t		*p_adapter,
		IN		ib_net64_t			sid )
{
	vnic_sid_t	svc_id;
	svc_id.as_uint64 = sid;
	if( ( svc_id.s.base_id & 0x10 ) != 0x10 )
		return FALSE;
	if( svc_id.s.oui[0] != 0x00 &&
		svc_id.s.oui[1] != 0x06 &&
		svc_id.s.oui[2] != 0x6a )
		return FALSE;
	if ( svc_id.s.type != CONTROL_SID &&
		 svc_id.s.type != DATA_SID )
		 return FALSE;
	if ( svc_id.s.ioc_num	!= _get_ioc_num_from_iocguid( &p_adapter->ifc_data.guid ) )
		return FALSE;
	return TRUE;
}
static inline uint8_t
_get_ioc_num_from_iocguid(
		  IN ib_net64_t		*p_iocguid )
{
	return ( (vnic_ioc_guid_t *)p_iocguid)->s.ioc_num;
}

static
vnic_path_record_t*
__path_record_add(
	IN				vnic_adapter_t* const	p_adapter,
	IN				ib_path_rec_t* const	p_path_rec )
{

	vnic_path_record_t		*p_path;

	p_path = __path_record_find( p_adapter, p_path_rec );

	if( !p_path )
	{
		p_path = cl_zalloc( sizeof( vnic_path_record_t ) );
		if( !p_path )
			return NULL;

		NdisAcquireSpinLock( &p_adapter->path_records_lock );
		
		p_path->path_rec = *p_path_rec;
		cl_qlist_insert_tail( &p_adapter->path_records_list, &p_path->list_entry );

		NdisReleaseSpinLock( &p_adapter->path_records_lock );

		VNIC_TRACE( VNIC_DBG_PNP,
			("New path Added to the list[ list size %d]\n", 
				cl_qlist_count(&p_adapter->path_records_list) ));
		
		return p_path;
	}
	VNIC_TRACE( VNIC_DBG_PNP,
			( "Path to add is already on the List\n" ) );

	return NULL;
}

static
vnic_path_record_t*
__path_record_get(
	IN				vnic_adapter_t* const	p_adapter )
{
	cl_list_item_t		*p_item;

	p_item = cl_qlist_head( &p_adapter->path_records_list );

	if( p_item != cl_qlist_end( &p_adapter->path_records_list ) )
	{
		return ( vnic_path_record_t *)p_item;
	}
	return NULL;
}

static 
vnic_path_record_t*
__path_record_remove(
		IN			vnic_adapter_t* const	p_adapter,
		IN			ib_path_rec_t* const	p_path_rec )
{

	vnic_path_record_t	*p_path;


	p_path = __path_record_find( p_adapter, p_path_rec );

	if ( p_path )
	{
		NdisAcquireSpinLock( &p_adapter->path_records_lock );

		cl_qlist_remove_item( &p_adapter->path_records_list, &p_path->list_entry );

		NdisReleaseSpinLock( &p_adapter->path_records_lock );
	}
	
	return p_path;
}

static
vnic_path_record_t *
__path_record_find( 
	IN		vnic_adapter_t* const	p_adapter,
	IN		ib_path_rec_t* const	p_path_rec )
{
	cl_list_item_t		*p_item;
	vnic_path_record_t	*p_path = NULL;

	NdisAcquireSpinLock( &p_adapter->path_records_lock );

	if( !cl_qlist_count( &p_adapter->path_records_list ) )
	{
		NdisReleaseSpinLock( &p_adapter->path_records_lock );
		return NULL;
	}
	
	p_item = cl_qlist_head( &p_adapter->path_records_list );

	while( p_item != cl_qlist_end( &p_adapter->path_records_list ) )
	{
		p_path = ( vnic_path_record_t *)p_item;
		
		if ( __path_records_match( &p_path->path_rec, p_path_rec ) )
		{
			break;
		}

		p_item = cl_qlist_next( p_item );
	}

	NdisReleaseSpinLock( &p_adapter->path_records_lock );

	if( p_item == cl_qlist_end( &p_adapter->path_records_list ) )
	{
		p_path = NULL;
	}
	return p_path;
}

void
__pending_queue_cleanup( 
	IN		vnic_adapter_t	*p_adapter )
{
	LIST_ENTRY		*p_list_item;
	NDIS_PACKET		*p_packet;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	/* clear pending queue if any */

	while( ( p_list_item = NdisInterlockedRemoveHeadList(
		&p_adapter->send_pending_list,
		&p_adapter->pending_list_lock )) != NULL )
	{
		p_packet = VNIC_PACKET_FROM_LIST_ITEM( p_list_item );
		if ( p_packet )
		{
			NDIS_SET_PACKET_STATUS( p_packet, NDIS_STATUS_FAILURE );
			NdisMSendComplete( p_adapter->h_handle,	p_packet, NDIS_STATUS_FAILURE );
		}
	}

	VNIC_EXIT( VNIC_DBG_ADAPTER );
}

static void
__path_records_cleanup(
		   vnic_adapter_t	*p_adapter )
{

	vnic_path_record_t	*p_path;
	cl_list_item_t	*p_item;

	p_item = cl_qlist_remove_head( &p_adapter->path_records_list );

	while( p_item != cl_qlist_end( &p_adapter->path_records_list ) )
	{
		p_path = (vnic_path_record_t *)p_item;

		cl_free( p_path );

		p_item = cl_qlist_remove_head( &p_adapter->path_records_list );
	}
	
	return;
}

ib_api_status_t
__vnic_pnp_cb(
	IN              ib_pnp_rec_t                *p_pnp_rec )
{
	ib_api_status_t			ib_status = IB_SUCCESS;
	ib_pnp_ioc_rec_t		*p_ioc_rec;
	ib_pnp_ioc_path_rec_t	*p_ioc_path;
	vnic_path_record_t		*p_path_record;
	Netpath_t*				p_netpath = NULL;

#if ( LBFO_ENABLED )
	vnic_adapter_t			*p_primary_adapter;	
#endif

	vnic_adapter_t * p_adapter = (vnic_adapter_t *)p_pnp_rec->pnp_context;
	
	VNIC_ENTER( VNIC_DBG_PNP );

	CL_ASSERT( p_adapter );

	switch( p_pnp_rec->pnp_event )
	{
		case IB_PNP_IOC_ADD:
			p_ioc_rec = (ib_pnp_ioc_rec_t*)p_pnp_rec;

			if( p_adapter->ifc_data.ca_guid != p_ioc_rec->ca_guid )
			{
				ib_status = IB_INVALID_GUID;
				break;
			}
			if( p_adapter->ifc_data.guid != p_ioc_rec->info.profile.ioc_guid )
			{
				ib_status = IB_INVALID_GUID;
				break;
			}
			InterlockedExchange( (volatile LONG*)&p_adapter->pnp_state, IB_PNP_IOC_ADD );

			VNIC_TRACE( VNIC_DBG_PNP, ("IB_PNP_IOC_ADD for %s.\n",
				p_ioc_rec->info.profile.id_string) );

			/* get ioc profile data */
			NdisAcquireSpinLock( &p_adapter->lock );
		
			if( !__sid_valid( p_adapter, p_ioc_rec->svc_entry_array[0].id ) )
			{
				NdisReleaseSpinLock( &p_adapter->lock );

				VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
					("Invalid Service ID %#I64x\n",p_ioc_rec->svc_entry_array[0].id ) );

				ib_status = IB_INVALID_GUID; // should it be set INVALID_SERVICE_TYPE ?
				break;
			}

			p_adapter->ioc_info = p_ioc_rec->info;
			CL_ASSERT(p_adapter->ioc_info.profile.num_svc_entries == 2 );
			
			p_adapter->svc_entries[0] = p_ioc_rec->svc_entry_array[0];
			p_adapter->svc_entries[1] = p_ioc_rec->svc_entry_array[1];
			p_adapter->ioc_num = _get_ioc_num_from_iocguid( &p_adapter->ifc_data.guid );

			VNIC_TRACE( VNIC_DBG_PNP,
				("Found %d Service Entries.\n", p_adapter->ioc_info.profile.num_svc_entries));

			NdisReleaseSpinLock( &p_adapter->lock );
			
			break;

		case IB_PNP_IOC_REMOVE:
			CL_ASSERT( p_pnp_rec->guid == p_adapter->ifc_data.guid );
		
			p_ioc_rec = (ib_pnp_ioc_rec_t*)p_pnp_rec;

			if( InterlockedExchange( (volatile LONG*)&p_adapter->pnp_state, IB_PNP_IOC_REMOVE ) == IB_PNP_IOC_ADD )
			{
				VNIC_TRACE( VNIC_DBG_INIT, ("IB_PNP_IOC_REMOVE for %s.\n",
							p_adapter->ioc_info.profile.id_string) );
			
				if( netpath_is_valid( p_adapter->p_currentPath ) )
				{
					netpath_stopXmit( p_adapter->p_currentPath );
					InterlockedExchange( &p_adapter->p_currentPath->carrier, (LONG)FALSE );
					netpath_linkDown( p_adapter->p_currentPath );
					__pending_queue_cleanup( p_adapter );
				}
			}
			break;

		case IB_PNP_IOC_PATH_ADD:
			/* path for our IOC ? */
			if ( p_pnp_rec->guid != p_adapter->ifc_data.guid )
			{
				VNIC_TRACE( VNIC_DBG_PNP,
					("Getting path for wrong IOC\n") );
				ib_status = IB_INVALID_GUID;
				break;
			}
			p_ioc_path = (ib_pnp_ioc_path_rec_t*)p_pnp_rec;

			p_path_record = __path_record_add( p_adapter, &p_ioc_path->path );
			if ( p_path_record == NULL )
			{
				VNIC_TRACE( VNIC_DBG_ERROR,
					("Failed to add path record\n") );
				break;
			}

			if( p_adapter->state == INIC_REGISTERED )
			{
				if(	p_adapter->num_paths > 0 )
				{
					if( p_adapter->params.SecondaryPath != TRUE )
					{
						VNIC_TRACE ( VNIC_DBG_WARN,
						("Allowed one path at a time\n") );
						break;
					}
				}

				if( p_adapter->num_paths > 1 )
				{
					VNIC_TRACE ( VNIC_DBG_WARN,
					("Max Paths[%d] Connected already\n", p_adapter->num_paths) );
					break;
				}
			}
			
			ib_status = adapter_netpath_update_and_connect( 
												p_adapter, p_path_record );
			if( ib_status != IB_SUCCESS )
			{
				VNIC_TRACE( VNIC_DBG_ERROR,
					("IOC[%d] adapter_netpath_update_and_connect return %s\n",	
					p_adapter->ioc_num,
					p_adapter->ifc.get_err_str( ib_status )) );
				break;
			}

			InterlockedCompareExchange( (volatile LONG *)&p_adapter->state,	
											INIC_REGISTERED, INIC_UNINITIALIZED );
#if ( LBFO_ENABLED )

			if( p_adapter->failover.fo_state == _ADAPTER_NOT_BUNDLED )
			{
				/* we don't look for zero id, since it meant to be primary by default */
				if( p_adapter->failover.bundle_id != 0 && 
					( p_primary_adapter = __adapter_find_on_failover_list( _ADAPTER_PRIMARY,
					  p_adapter->failover.bundle_id ) ) != NULL )
				{
						/* found matching primary */
						__adapter_set_failover_secondary(
								p_adapter, p_primary_adapter->h_handle );
				}
				else
				{
					/* bundle_id '0' , all go to primary */
					__adapter_set_failover_primary( p_adapter, FALSE );
				}
			}

#endif // LBFO_ENABLED

			break;

		case IB_PNP_IOC_PATH_REMOVE:
			p_ioc_path = (ib_pnp_ioc_path_rec_t*)p_pnp_rec;

			VNIC_TRACE( VNIC_DBG_PNP,
				("IB_PNP_IOC_PATH_REMOVE (slid:%d dlid:%d) for %s.\n",
				ntoh16( p_ioc_path->path.slid ),
				ntoh16( p_ioc_path->path.dlid ),
				p_adapter->ioc_info.profile.id_string));

			p_path_record = __path_record_remove( p_adapter, &p_ioc_path->path );

			if ( p_path_record != NULL )
			{
				int fail_over = 0;

				if( p_adapter->p_currentPath->p_path_rec != &p_path_record->path_rec )
				{
					VNIC_TRACE( VNIC_DBG_INFO,
						("IOC[%d] Standby Path lost\n", p_adapter->ioc_num ));

					if( p_adapter->p_currentPath !=  &p_adapter->primaryPath )
					{
						_adapter_netpath_free( p_adapter,  &p_adapter->primaryPath );
					}
					else
					{
						_adapter_netpath_free( p_adapter, &p_adapter->secondaryPath );
					}

					cl_free( p_path_record );
					break;
				}

				VNIC_TRACE( VNIC_DBG_ERROR,
					("IOC[%d]Current Path lost\n", p_adapter->ioc_num ));
				
				p_netpath = p_adapter->p_currentPath;
				netpath_stopXmit( p_netpath );
				viport_timerStop( p_netpath->pViport );

				if( !p_adapter->params.SecondaryPath )
				{
					vnic_path_record_t* p_record;

					netpath_linkDown( p_netpath );
					_adapter_netpath_free(p_adapter, p_netpath );

					p_record = __path_record_get( p_adapter );
					if( p_record )
					{
						ib_status = adapter_netpath_update_and_connect( p_adapter, p_record );
					}
					
					cl_free( p_path_record );
					break;
				}

				if( p_netpath == &p_adapter->primaryPath )
				{
					if( netpath_is_connected( &p_adapter->secondaryPath ) )
					{
						netpath_stopXmit( &p_adapter->secondaryPath );
						p_adapter->p_currentPath = &p_adapter->secondaryPath;
						fail_over = 1;
						VNIC_TRACE( VNIC_DBG_ERROR,
							("IOC[%d]Switch to Secondary Path\n", p_adapter->ioc_num ));
					}
				}
				else
				{
					if( netpath_is_connected( &p_adapter->primaryPath ) )
					{
						netpath_stopXmit( &p_adapter->primaryPath );
						p_adapter->p_currentPath = &p_adapter->primaryPath;
						fail_over = 1;
						VNIC_TRACE( VNIC_DBG_ERROR,
							("IOC[%d]Switch to Primary Path\n", p_adapter->ioc_num ));
					}
				}
				if( fail_over )
				{
					viport_setLink( p_adapter->p_currentPath->pViport,
								INIC_FLAG_ENABLE_NIC| INIC_FLAG_SET_MTU,
								(uint16_t)p_adapter->params.MinMtu, FALSE );
				}

				_adapter_netpath_free(p_adapter, p_netpath );
				cl_free( p_path_record );
			}

			break;

		default:
			VNIC_TRACE( VNIC_DBG_PNP,
				(" Received unhandled PnP event %#x\n",	p_pnp_rec->pnp_event ) );
			break;
	}

	VNIC_EXIT( VNIC_DBG_PNP );
	return ib_status;
}

NDIS_STATUS
vnic_get_adapter_interface(
	IN	NDIS_HANDLE			h_handle,
	IN	vnic_adapter_t		*p_adapter)
{
	NTSTATUS			status;
	ib_al_ifc_data_t	data;
	IO_STACK_LOCATION	io_stack;

	VNIC_ENTER( VNIC_DBG_ADAPTER );

	NdisMGetDeviceProperty( h_handle, &p_adapter->p_pdo, NULL, NULL, NULL, NULL );

	data.size = sizeof(ioc_ifc_data_t);
	data.type = &GUID_IOC_INTERFACE_DATA;
	data.version = IOC_INTERFACE_DATA_VERSION;
	data.p_data = &p_adapter->ifc_data;

	io_stack.MinorFunction = IRP_MN_QUERY_INTERFACE;
	io_stack.Parameters.QueryInterface.Version = AL_INTERFACE_VERSION;
	io_stack.Parameters.QueryInterface.Size = sizeof(ib_al_ifc_t);
	io_stack.Parameters.QueryInterface.Interface = (INTERFACE*)&p_adapter->ifc;
	io_stack.Parameters.QueryInterface.InterfaceSpecificData = &data;
	io_stack.Parameters.QueryInterface.InterfaceType = &GUID_IB_AL_INTERFACE;

	status = cl_fwd_query_ifc( p_adapter->p_pdo, &io_stack );

	if( !NT_SUCCESS( status ) )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Query interface for VNIC interface returned %08x.\n", status) );
		return status;
	}
	/*
	 * Dereference the interface now so that the bus driver doesn't fail a
	 * query remove IRP.  We will always get unloaded before the bus driver
	 * since we're a child device.
	 */
	p_adapter->ifc.wdm.InterfaceDereference(
		p_adapter->ifc.wdm.Context );

	VNIC_EXIT( VNIC_DBG_ADAPTER );

	return NDIS_STATUS_SUCCESS;
}


static void
__adapter_cleanup(
		IN	vnic_adapter_t		*p_adapter )
{
	__pending_queue_cleanup( p_adapter );

	__path_records_cleanup( p_adapter );

	_adapter_netpath_free( p_adapter, &p_adapter->primaryPath );
	_adapter_netpath_free( p_adapter, &p_adapter->secondaryPath );

	VNIC_EXIT( VNIC_DBG_ADAPTER );
}

void
__vnic_pnp_dereg_cb(
	IN			void*			context )
{
	vnic_adapter_t*		p_adapter;
	ib_api_status_t		ib_status;
	ib_pnp_req_t		pnp_req;

	NDIS_STATUS			ndis_status = NDIS_STATUS_SUCCESS;

	VNIC_ENTER( VNIC_DBG_INIT );

	p_adapter = (vnic_adapter_t*)context;

	CL_ASSERT( !p_adapter->h_pnp );

	/* Destroy port instances if still exist. */
	__adapter_cleanup( p_adapter );
	
	if( p_adapter->pnp_state != IB_PNP_IOC_REMOVE )
	{
		p_adapter->pnp_state = IB_PNP_IOC_ADD;
		/* Register for IOC events */
		pnp_req.pfn_pnp_cb = __vnic_pnp_cb;
		pnp_req.pnp_class = IB_PNP_IOC | IB_PNP_FLAG_REG_SYNC;
		pnp_req.pnp_context = (const void *)p_adapter;

		ib_status = p_adapter->ifc.reg_pnp( p_adapter->h_al, &pnp_req, &p_adapter->h_pnp );
		if( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
						("pnp_reg returned %s\n",
								p_adapter->ifc.get_err_str( ib_status )) );
			ndis_status = NDIS_STATUS_HARD_ERRORS;
		}
	}
	else
	{
		ndis_status = NDIS_STATUS_HARD_ERRORS;
	}

	if( p_adapter->reset )
	{
		p_adapter->reset = FALSE;
		NdisMResetComplete( 
			p_adapter->h_handle, ndis_status, TRUE );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
}


ib_api_status_t
adapter_reset(
	   IN	vnic_adapter_t* const	p_adapter )
{

	ib_api_status_t		status;
	ib_pnp_handle_t		h_pnp;

	VNIC_ENTER( VNIC_DBG_INIT );

	if( p_adapter->reset )
		return IB_INVALID_STATE;

	p_adapter->reset = TRUE;
	p_adapter->hung = 0;

#if ( LBFO_ENABLED )
	__adapter_remove_from_failover_list( p_adapter );

#endif // LBFO_ENABLED

	if( p_adapter->h_pnp )
	{
		h_pnp = p_adapter->h_pnp;
		p_adapter->h_pnp  = NULL;
		status = p_adapter->ifc.dereg_pnp( h_pnp, __vnic_pnp_dereg_cb );
		if( status == IB_SUCCESS )
			status = IB_NOT_DONE;
	}
	else
	{
		status = IB_NOT_FOUND;
	}
	
	VNIC_EXIT( VNIC_DBG_INIT );
	return status;
}

static
NDIS_STATUS
_adapter_set_sg_size(
		 IN		NDIS_HANDLE		h_handle,
		 IN		ULONG			mtu_size )
{
	NDIS_STATUS		status;
	ULONG			buf_size;

	VNIC_ENTER( VNIC_DBG_INIT );

	buf_size = mtu_size + ETH_VLAN_HLEN;

	status = NdisMInitializeScatterGatherDma( h_handle, TRUE, buf_size );

	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("Init ScatterGatherDma failed status %#x\n", status ) );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
	return status;
}

static ib_api_status_t
_adapter_close_ca(
		IN		vnic_adapter_t* const	p_adapter )
{
	ib_api_status_t		ib_status = IB_SUCCESS;
	ib_ca_handle_t		h_ca;

	VNIC_ENTER( VNIC_DBG_INIT );

	if( !p_adapter )
		return ib_status;

	VNIC_TRACE(VNIC_DBG_INIT,
		("IOC[%d] Close CA\n", p_adapter->ioc_num ));

	if( p_adapter->ca.region.h_mr )
	{
		ib_status = p_adapter->ifc.dereg_mr( p_adapter->ca.region.h_mr );
		if( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("Failed to dereg MR\n"));
		}
		p_adapter->ca.region.h_mr = NULL;
	}

	if( p_adapter->ca.hPd )
	{
		ib_status = p_adapter->ifc.dealloc_pd( p_adapter->ca.hPd, NULL );
		if( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("Failed to dealloc PD\n"));
		}
		p_adapter->ca.hPd = NULL;
	}

	if( ( h_ca = InterlockedExchangePointer( (void *)&p_adapter->h_ca, NULL ) ) != NULL )
	{

		ib_status = p_adapter->ifc.close_ca( h_ca, NULL );
		if( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("Failed to close CA\n"));
		}
	}

	VNIC_EXIT( VNIC_DBG_INIT );
	return ib_status;
}

static 
ib_api_status_t
_adapter_netpath_update(
	IN		vnic_adapter_t* const	p_adapter,
	IN		viport_t* const			p_viport,
	IN		vnic_path_record_t*		p_path )
{

	Netpath_t			*p_netpath_init;

	VNIC_ENTER( VNIC_DBG_PNP );

	NdisAcquireSpinLock( &p_adapter->lock );
	/* set primary first */
	if( !netpath_is_valid( p_adapter->p_currentPath ) )
	{
		p_netpath_init = &p_adapter->primaryPath;
		p_adapter->p_currentPath = p_netpath_init;
	}

	else 
	{
		if( p_adapter->p_currentPath != &p_adapter->primaryPath )
		{
			p_netpath_init = &p_adapter->primaryPath;
		}
		else
		{
			p_netpath_init = &p_adapter->secondaryPath;
		}
	}

	/* shouldn't really happened ?? */
	if( netpath_is_connected( p_netpath_init ) )
	{
		VNIC_TRACE( VNIC_DBG_WARN,
			("No available Netpath found for update\n"));

		NdisReleaseSpinLock( &p_adapter->lock );
		return IB_NO_MATCH;
	}

	p_netpath_init->p_path_rec = &p_path->path_rec;

	/* netpath initialization */

	p_netpath_init->p_adapter = p_adapter;
	p_netpath_init->pViport = p_viport;
	p_netpath_init->carrier = FALSE;

	/* viport initialization */
	p_viport->p_netpath = p_netpath_init;

	p_viport->portGuid = p_netpath_init->p_path_rec->sgid.unicast.interface_id;
	p_viport->port_config.dataConfig.ibConfig.pathInfo = *p_netpath_init->p_path_rec;
	p_viport->port_config.controlConfig.ibConfig.pathInfo = *p_netpath_init->p_path_rec;
	
	/* set our instance id per IOC */
	p_viport->port_config.controlConfig.ibConfig.connData.inicInstance = (uint8_t)p_netpath_init->instance;
	p_viport->port_config.dataConfig.ibConfig.connData.inicInstance = (uint8_t)p_netpath_init->instance;
	/* save for later use */
	p_viport->port_config.controlConfig.inicInstance = (uint8_t)p_netpath_init->instance;
		
	p_viport->iocGuid  = p_adapter->ioc_info.profile.ioc_guid;
	p_viport->port_config.controlConfig.ibConfig.sid = p_adapter->svc_entries[0].id;
	p_viport->port_config.dataConfig.ibConfig.sid = p_adapter->svc_entries[1].id;

		VNIC_TRACE( VNIC_DBG_INFO,
		("IOC[%d] instance %d [%s] is using SLID=%d DLID=%d Target:%s\n",
		p_viport->ioc_num,
		p_netpath_init->instance,
		netpath_to_string( p_netpath_init ),
		cl_ntoh16( p_path->path_rec.slid ),
		cl_ntoh16( p_path->path_rec.dlid),
		p_adapter->ioc_info.profile.id_string) );
	
	NdisReleaseSpinLock( &p_adapter->lock );

	VNIC_EXIT( VNIC_DBG_PNP );
	return IB_SUCCESS;
}

static
ib_api_status_t
adapter_netpath_update_and_connect(
	IN		vnic_adapter_t*	const	p_adapter,
	IN		vnic_path_record_t*		p_path )
{
	ib_api_status_t		ib_status;
	viport_t*			p_viport;

	ib_status = adapter_viport_allocate( p_adapter, &p_viport );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE ( VNIC_DBG_ERROR,
			("IOC[%] Viport allocate Failed status %s\n",
			p_adapter->ioc_num,	p_adapter->ifc.get_err_str( ib_status )) );
		goto err;
	}

	ib_status = _adapter_open_ca( p_adapter );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d]Open CA failed %s\n", p_adapter->ioc_num,
				p_adapter->ifc.get_err_str( ib_status )) );
		goto err;
	}

	ib_status = _adapter_netpath_update( p_adapter, p_viport, p_path );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d]Update New Path failed %s\n", p_adapter->ioc_num,
				p_adapter->ifc.get_err_str( ib_status )) );
		goto err;
	}

	ib_status = viport_control_connect( p_viport );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d]Control QP connect return %s\n", p_adapter->ioc_num,
				p_adapter->ifc.get_err_str( ib_status )) );
		goto err;
	}

	/* should call this only once */
	if( p_adapter->state == INIC_UNINITIALIZED )
	{
		if( _adapter_set_sg_size( p_adapter->h_handle, 
			p_adapter->params.MinMtu ) != NDIS_STATUS_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d]ScatterGather Init using MTU size %d failed\n", p_adapter->ioc_num,
				p_adapter->params.MinMtu ) );
			goto err;
		}
	}

	ib_status = viport_data_connect( p_viport );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d]Data QP connect return %s\n", p_adapter->ioc_num,
				p_adapter->ifc.get_err_str( ib_status )) );
		goto err;
	}
	
	VNIC_TRACE( VNIC_DBG_INFO,
			("IOC[%d] instance %d %s is CONNECTED\n",
			p_viport->ioc_num,
			p_viport->p_netpath->instance,
			netpath_to_string( p_viport->p_netpath ) ));

	if( p_viport->state == VIPORT_CONNECTED )
	{
		p_adapter->num_paths++;
	}

	if( p_adapter->num_paths > 1 &&
		p_viport->p_netpath != p_adapter->p_currentPath )
	{
		if ( !netpath_setUnicast( p_viport->p_netpath, 
			p_adapter->p_currentPath->pViport->hwMacAddress ) )
		{
			ib_status = IB_ERROR;
			goto err;
		}
	}

	return ib_status;

err:
	VNIC_TRACE( VNIC_DBG_ERROR,
		("IOC[%d] allocate return %#x (%s)\n", p_adapter->ioc_num,
				ib_status, p_adapter->ifc.get_err_str( ib_status )) );

	if( p_viport != NULL &&  
		p_viport->p_netpath != NULL )
	{
		netpath_free( p_viport->p_netpath );
	}
	return ib_status;
}

#if ( LBFO_ENABLED )

static void
__adapter_add_to_failover_list(
	 IN		vnic_adapter_t* const	p_adapter,
	 IN		lbfo_state_t			state )
{

	CL_ASSERT( p_adapter );
	CL_ASSERT( state == _ADAPTER_PRIMARY || state == _ADAPTER_SECONDARY );

	p_adapter->failover.fo_state = state;

	if( state == _ADAPTER_PRIMARY )
	{	
		p_adapter->failover.primary_handle = p_adapter;
		cl_qlist_insert_tail( &g_vnic.primary_list, &p_adapter->list_item );
	}
	else
	{
		cl_qlist_insert_tail( &g_vnic.secondary_list, &p_adapter->list_item );
	}
}

static void
__adapter_remove_from_failover_list(
	 IN		vnic_adapter_t* const	p_adapter )
{
	lbfo_state_t lbfo_state;
	vnic_adapter_t	*p_adapter_to_promote;
	uint32_t		bundle_id;

	CL_ASSERT( p_adapter );

	lbfo_state = p_adapter->failover.fo_state;
	p_adapter->failover.fo_state = _ADAPTER_NOT_BUNDLED;
	bundle_id = p_adapter->failover.bundle_id;

	if( lbfo_state == _ADAPTER_PRIMARY )
	{
		cl_qlist_remove_item( &g_vnic.primary_list, &p_adapter->list_item );
	
		/* search for secondary adapter with same id && (id != 0 ) */
		if( bundle_id != 0 )
		{
			p_adapter_to_promote =
				__adapter_find_on_failover_list( _ADAPTER_SECONDARY, bundle_id );
		
			if( p_adapter_to_promote &&
				p_adapter_to_promote->pnp_state != IB_PNP_IOC_REMOVE &&
				p_adapter_to_promote->state == INIC_REGISTERED &&
				p_adapter_to_promote->reset == FALSE )
			{
				/* a small recursion */
				__adapter_set_failover_primary( p_adapter_to_promote, TRUE );
			}
		}
	}
	else if( lbfo_state == _ADAPTER_SECONDARY )
	{
		cl_qlist_remove_item( &g_vnic.secondary_list, &p_adapter->list_item );
		VNIC_TRACE( VNIC_DBG_INFO,
				("IOC[%d]  LBFO bundle %d Secondary Adapter Removed\n", 
				p_adapter->ioc_num, p_adapter->failover.bundle_id ));
	}
	else if( lbfo_state == _ADAPTER_NOT_BUNDLED )
	{
		VNIC_TRACE( VNIC_DBG_INFO,
		("IOC[%d] Adapter not bundled\n", p_adapter->ioc_num ));
	}

	return;
}


static vnic_adapter_t*
__adapter_find_on_failover_list(
	IN		lbfo_state_t	list_flag,
	IN		uint32_t		bundle_id )
{
	vnic_adapter_t		*p_adapter = NULL;
	cl_list_item_t		*p_item;
	cl_qlist_t			*p_qlist = ( list_flag == _ADAPTER_PRIMARY ) ?
		&g_vnic.primary_list : &g_vnic.secondary_list;
	
	p_item = cl_qlist_head( p_qlist );

	while( p_item != cl_qlist_end( p_qlist ) )
	{
		p_adapter = PARENT_STRUCT( p_item, vnic_adapter_t, list_item );

		if( p_adapter &&
			p_adapter->failover.bundle_id == bundle_id )
		{
			return p_adapter;
		}

		p_item = cl_qlist_next( p_item );
	}
	return NULL;
}

static NDIS_STATUS
__adapter_set_failover_primary(
	 IN		vnic_adapter_t*  const p_adapter,
	 IN		BOOLEAN			promote_secondary )
{
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
	
	CL_ASSERT( p_adapter );
	
	if( promote_secondary )
	{
		if( p_adapter->failover.fo_state != _ADAPTER_SECONDARY )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("LBFO Can't promote NON_SECONDARY\n"));
			return NDIS_STATUS_NOT_ACCEPTED;
		}
		__adapter_remove_from_failover_list( p_adapter );
		status = NdisMPromoteMiniport( p_adapter->h_handle );
	}

	if( status == NDIS_STATUS_SUCCESS )
	{
		p_adapter->failover.p_adapter = p_adapter;
		p_adapter->failover.fo_state = _ADAPTER_PRIMARY;
		p_adapter->failover.primary_handle = p_adapter->h_handle;
		__adapter_add_to_failover_list( p_adapter, _ADAPTER_PRIMARY );

		VNIC_TRACE( VNIC_DBG_INFO,
			("IOC[%d] Set LBFO bundle %d Primary Adapter\n", 
			p_adapter->ioc_num, p_adapter->failover.bundle_id ));
	}
	else
		VNIC_TRACE( VNIC_DBG_ERROR,
				("LBFO Set to Primary Failed\n"));

	return status;
}

static NDIS_STATUS
__adapter_set_failover_secondary(
	 IN		vnic_adapter_t* const	p_adapter,
	 IN		NDIS_HANDLE				primary_handle )
{
	NDIS_STATUS	status;

	CL_ASSERT( p_adapter );

	status = NdisMSetMiniportSecondary( p_adapter->h_handle, primary_handle );
	
	if ( status == NDIS_STATUS_SUCCESS )
	{
		p_adapter->failover.fo_state = _ADAPTER_SECONDARY;
		p_adapter->failover.primary_handle = primary_handle;
		__adapter_add_to_failover_list( p_adapter, _ADAPTER_SECONDARY );

		VNIC_TRACE( VNIC_DBG_INFO,
				("IOC[%d] Set LBFO bundle %d Secondary Adapter\n", 
				p_adapter->ioc_num, p_adapter->failover.bundle_id ));
	}

	return status;
}

#endif //LBFO_ENABLED
