/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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
#ifndef _VNIC_VIPORT_H_
#define _VNIC_VIPORT_H_

typedef struct _mc_list {
	uint8_t				mc_addr[MAC_ADDR_LEN];
} mc_list_t;

typedef enum {
	VIPORT_DISCONNECTED,
	VIPORT_CONNECTED
} viport_state_t;

typedef enum {
	LINK_UNINITIALIZED,
	LINK_INITIALIZE,
	LINK_INITIALIZECONTROL,
	LINK_INITIALIZEDATA,
	LINK_CONTROLCONNECT,
	LINK_CONTROLCONNECTWAIT,
	LINK_INITINICREQ,
	LINK_INITINICRSP,
	LINK_BEGINDATAPATH,
	LINK_CONFIGDATAPATHREQ,
	LINK_CONFIGDATAPATHRSP,
	LINK_DATACONNECT,
	LINK_DATACONNECTWAIT,
	LINK_XCHGPOOLREQ,
	LINK_XCHGPOOLRSP,
	LINK_INITIALIZED,
	LINK_IDLE,
	LINK_IDLING,
	LINK_CONFIGLINKREQ,
	LINK_CONFIGLINKRSP,
	LINK_CONFIGADDRSREQ,
	LINK_CONFIGADDRSRSP,
	LINK_REPORTSTATREQ,
	LINK_REPORTSTATRSP,
	LINK_HEARTBEATREQ,
	LINK_HEARTBEATRSP,
	LINK_RESET,
	LINK_RESETRSP,
	LINK_RESETCONTROL,
	LINK_RESETCONTROLRSP,
	LINK_DATADISCONNECT,
	LINK_CONTROLDISCONNECT,
	LINK_CLEANUPDATA,
	LINK_CLEANUPCONTROL,
	LINK_DISCONNECTED,
	LINK_RETRYWAIT
} LinkState_t;

/* index entries */
#define BROADCAST_ADDR       0
#define UNICAST_ADDR         1
#define MCAST_ADDR_START     2
#define MAX_MCAST		(MAX_ADDR_ARRAY - MCAST_ADDR_START)

#define currentMacAddress    macAddresses[UNICAST_ADDR].address

#define NEED_STATS           0x00000001
#define NEED_ADDRESS_CONFIG  0x00000002
#define NEED_LINK_CONFIG     0x00000004
#define MCAST_OVERFLOW       0x00000008
#define SYNC_QUERY			0x80000000

typedef enum {
        NETPATH_TS_IDLE,
        NETPATH_TS_ACTIVE,
        NETPATH_TS_EXPIRED
} netpathTS_t;


typedef struct {
	LIST_ENTRY				listPtrs;
	struct _vnic_adapter    *p_adapter;
	uint8_t					event_num;
} InicNPEvent_t;

typedef enum {
	NETPATH_PRIMARY = 1,
	NETPATH_SECONDARY = 2
} netpath_instance_t;


typedef struct Netpath {
		volatile LONG			carrier;
		struct _vnic_adapter	*p_adapter;
		struct _viport			*pViport;
		ib_path_rec_t			*p_path_rec;
		uint64_t				connectTime;
		netpathTS_t				timerState;
		netpath_instance_t		instance;
} Netpath_t;

typedef enum {
	WAIT,
	DELAY,
	NOW
} conn_wait_state_t;

typedef struct _viport {
	LIST_ENTRY						listPtrs;
	NDIS_SPIN_LOCK					lock;
	cl_obj_t						obj;
	struct _vnic_adapter			*p_adapter;
	struct Netpath					*p_netpath;
	struct ViportConfig				port_config;
	struct Control					control;
	struct Data						data;
	uint64_t						iocGuid;
	uint64_t						portGuid;
	uint32_t						ioc_num;
	// connected/disconnected state of control and data QPs.
	viport_state_t					state;

	// Control Path cmd state Query/Rsp
	LinkState_t						linkState;
	LinkState_t						link_hb_state;
	Inic_CmdReportStatisticsRsp_t	stats;
	uint64_t						lastStatsTime;
	uint32_t						featuresSupported;
	uint8_t							hwMacAddress[MAC_ADDR_LEN];
	uint16_t						defaultVlan;
	uint16_t						numMacAddresses;
	Inic_AddressOp_t				*macAddresses;
	int32_t							addrs_query_done;

	// Indicates actions (to the VEx) that need to be taken.
	volatile LONG					updates;

	uint8_t							flags;
	uint8_t							newFlags;
	uint16_t						mtu;
	uint16_t						newMtu;
	uint32_t						errored;
	uint32_t						disconnect;
	volatile LONG					timerActive;
	cl_timer_t						timer;
	cl_event_t						sync_event;
	BOOLEAN							hb_send_pending;
} viport_t;


BOOLEAN
viport_xmitPacket(
	IN		viport_t*	const	p_viport,
	IN		NDIS_PACKET* const	p_pkt );

BOOLEAN 
viport_config_defaults(
		IN	viport_t   *p_viport );

uint32_t
viport_get_adapter_name(
		IN	viport_t	*p_viport );

void     
viport_cleanup(
	IN		viport_t *p_viport );

BOOLEAN  
viport_setParent(
	IN		viport_t *pViport,
	IN		struct Netpath *pNetpath);

NDIS_STATUS  
viport_setLink(
	IN		viport_t *pViport, 
	IN		uint8_t flags,
	IN		uint16_t mtu,
	IN		BOOLEAN	 sync );

NDIS_STATUS  
viport_getStats(
	IN		viport_t *pViport );

void    
viport_timer(
	IN		viport_t	*p_viport,
	IN		int			timeout );

void    
viport_timerStop(
	IN		viport_t *p_viport );

void     
viport_linkUp(
	IN		viport_t *pViport);

void     
viport_linkDown(
	IN		viport_t *pViport);

void     
viport_stopXmit(
	IN		viport_t *pViport);

void     
viport_restartXmit(
	IN		viport_t		*pViport);

void     
viport_recvPacket(
	IN		viport_t		*pViport,
	IN		NDIS_PACKET		**p_pkt,
	IN		uint32_t		num_pkts );

void     
viport_failure(
	IN		viport_t *pViport);

BOOLEAN
viport_setUnicast(
	IN		viport_t* const		pViport, 
	IN		uint8_t *			pAddress);

NDIS_STATUS 
viport_setMulticast(
	IN		viport_t* const		pViport );

void    
netpath_init(
	IN		struct Netpath *pNetpath,
	IN		struct _vnic_adapter *p_adapter );

BOOLEAN 
netpath_addPath(
	IN		struct Netpath *pNetpath,
		IN		viport_t *pViport);

BOOLEAN
viport_unsetParent(
	IN		viport_t		*pViport );

ib_api_status_t
viport_control_connect(
	   IN	viport_t*	const	p_viport );

ib_api_status_t
viport_data_connect(
	   IN	viport_t*	const	p_viport );

NDIS_STATUS
_viport_process_query(
		IN		viport_t*	const	p_viport,
		IN		BOOLEAN				sync );

void
viport_cancel_xmit( 
	   IN	viport_t	*p_viport, 
	   IN	void		*p_cancel_id );

BOOLEAN 
netpath_removePath(
	IN		struct Netpath		*pNetpath,
	IN		viport_t			*p_viport );

void
netpath_free(
	IN	struct Netpath	*pNetpath );

BOOLEAN 
netpath_getStats(
	IN		struct Netpath *pNetpath );

NDIS_STATUS 
netpath_setMulticast(
	IN		struct Netpath *pNetpath );

int
netpath_maxMtu(
	IN		struct Netpath *pNetpath);

BOOLEAN
netpath_linkDown(
	IN		Netpath_t*		p_netpath );


BOOLEAN 
netpath_linkUp(
	IN		Netpath_t*		p_netpath );

BOOLEAN
netpath_is_valid(
	IN		Netpath_t*	const	p_netpath );

BOOLEAN
netpath_is_connected(
 	IN		Netpath_t*	const	p_netpath );

BOOLEAN
netpath_is_primary(
	IN		Netpath_t*	const	p_netpath );

BOOLEAN
netpath_xmitPacket(
	IN		struct Netpath*			pNetpath,
	IN		NDIS_PACKET*  const		p_pkt );

void
netpath_cancel_xmit( 
	IN		struct Netpath*		p_netpath, 
	IN		PVOID				cancel_id );

void   
netpath_recvPacket(
	IN		struct Netpath*		pNetpath,
	IN		NDIS_PACKET**		pp_pkt,
	IN		uint32_t			num_packets);

void
netpath_stopXmit(
	IN		struct Netpath *pNetpath );

void
netpath_restartXmit(
	IN		struct Netpath *pNetpath );

void
netpath_kick(
	IN		struct Netpath *pNetpath);

void
netpath_timer(
	IN		struct Netpath *pNetpath,
	IN		int timeout);

void
netpath_tx_timeout(
	IN		struct Netpath *pNetpath);

const char*
netpath_to_string(
	IN		struct Netpath *pNetpath );

BOOLEAN
netpath_setUnicast(
	IN		Netpath_t*		p_netpath,
	IN		uint8_t*		p_address );

BOOLEAN
viport_canTxCsum(
	IN		viport_t*		p_viport );

BOOLEAN
viport_canRxCsum(
	IN		viport_t*		p_viport );

#define  viport_portGuid(pViport) ((pViport)->portGuid)
#define  viport_maxMtu(pViport) data_maxMtu(&(pViport)->data)

#define  viport_getHwAddr(pViport,pAddress) \
			cl_memcpy(pAddress, (pViport)->hwMacAddress, MAC_ADDR_LEN)

#define  viport_features(pViport) ( (pViport)->featuresSupported )

#define netpath_getHwAddr(pNetpath, pAddress) \
			viport_getHwAddr((pNetpath)->pViport, pAddress)

#define netpath_canTxCsum(pNetpath) \
			viport_canTxCsum( (pNetpath)->pViport )

#define netpath_canRxCsum(pNetpath) \
			viport_canRxCsum( (pNetpath)->pViport )

#endif /* _VNIC_VIPORT_H_ */
