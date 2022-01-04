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
#ifndef _VNIC_CONFIG_H_
#define _VNIC_CONFIG_H_

#include "vnic_util.h"
/* These are hard, compile time limits.
 * Lower runtime overrides may be in effect
 */
#define INIC_CLASS_SUBCLASS 0x2000066A
#define INIC_PROTOCOL       0
#define INIC_PROT_VERSION   1

#define INIC_MAJORVERSION 1
#define INIC_MINORVERSION 1

#define LIMIT_OUTSTANDING_SENDS 0

#define MAX_ADDRESS_ENTRIES             64   /* max entries to negotiate with remote */
#define MIN_ADDRESS_ENTRIES             16   /* min entries remote can return to us that we agree with */
#define MAX_ADDR_ARRAY                  32   /* address array we can handle. for now */
#define MIN_MTU                         1500 /* Minimum Negotiated payload size */
#define MAX_MTU                         9500 /*  max Jumbo frame payload size */
#define ETH_VLAN_HLEN                   18   /* ethernet header with VLAN tag */

#define HOST_RECV_POOL_ENTRIES          512  /* TBD: Abritrary */
#define MIN_HOST_POOL_SZ                256   /* TBD: Abritrary */
#define MIN_EIOC_POOL_SZ                256   /* TBD: Abritrary */
#define MAX_EIOC_POOL_SZ                512  /* TBD: Abritrary */

#define MIN_HOST_KICK_TIMEOUT           100   /* TBD: Arbitrary */
#define MAX_HOST_KICK_TIMEOUT           200  /* In uSec */

#define MIN_HOST_KICK_ENTRIES           1    /* TBD: Arbitrary */
#define MAX_HOST_KICK_ENTRIES           128  /* TBD: Arbitrary */

#define MIN_HOST_KICK_BYTES             0
#define MAX_HOST_KICK_BYTES             5000

#define MIN_HOST_UPDATE_SZ              8    /* TBD: Arbitrary */
#define MAX_HOST_UPDATE_SZ              32   /* TBD: Arbitrary */
#define MIN_EIOC_UPDATE_SZ              8    /* TBD: Arbitrary */
#define MAX_EIOC_UPDATE_SZ              32   /* TBD: Arbitrary */

#define NOTIFY_BUNDLE_SZ                32

#define MAX_PARAM_VALUE                 0x40000000

#define DEFAULT_VIPORTS_PER_NETPATH     1
#define MAX_VIPORTS_PER_NETPATH         1

#define INIC_USE_RX_CSUM                TRUE
#define INIC_USE_TX_CSUM                TRUE
#define DEFAULT_NO_PATH_TIMEOUT         10000 /* TBD: Arbitrary */
#define DEFAULT_PRI_CON_TIMEOUT         10000 /* TBD: Arbitrary */
#define DEFAULT_PRI_RECON_TIMEOUT       10000 /* TBD: Arbitrary */
#define DEFAULT_PRI_SWITCH_TIMEOUT      10000 /* TBD: Arbitrary */
#define DEFAULT_PREFER_PRIMARY          TRUE

/* timeouts: !! all data defined in milliseconds,
   some later will be converted to microseconds */
#define VIPORT_STATS_INTERVAL           5000   /* 5 sec */
#define VIPORT_HEARTBEAT_INTERVAL       2000 /* 2 seconds */
#define VIPORT_HEARTBEAT_TIMEOUT        60000 /* 60 sec  */
#define CONTROL_RSP_TIMEOUT             2000  /* 2 sec */

#define _100NS_IN_1MS					(10000)
inline uint64_t
get_time_stamp_ms( void )
{
	return( (KeQueryInterruptTime() / _100NS_IN_1MS ) );
}

/* InfiniBand Connection Parameters */
#define CONTROL_REQ_RETRY_COUNT         4
#define RETRY_COUNT                     3
#define MIN_RNR_TIMER                   22       /* 20 ms */
#define DEFAULT_PKEY                    0    /* Pkey table index */

/* phys memory size to register with HCA*/
#define MEM_REG_SIZE    0xFFFFFFFFFFFFFFFF

/* link speed in 100 bits/sec units */
#define LINK_SPEED_1MBIT_x100BPS	10000
#define LINK_SPEED_1GBIT_x100BPS	10000000
#define LINK_SPEED_10GBIT_x100BPS	100000000
    /* if VEx does not report it's link speed, so set it 1Gb/s so far */
#define DEFAULT_LINK_SPEED_x100BPS	LINK_SPEED_1GBIT_x100BPS

#define DEFAULT_PARAM(x,y)	if(x == MAXU32) { \
								x = y; }
#define POWER_OF_2(x) if (!IsPowerOf2(x)) { \
                VNIC_TRACE( VNIC_DBG_WARN, (" %s (%d) must be a power of 2\n",#x,x) ); \
                x = SetMinPowerOf2(x); \
        }
#define LESS_THAN(lo, hi) if (lo >= hi) { \
                VNIC_TRACE( VNIC_DBG_ERROR, (" %s (%d) must be less than %s (%d)\n",#lo,lo,#hi,hi) ); \
                lo = hi >> 1; \
        }
#define LESS_THAN_OR_EQUAL(lo, hi) if (lo > hi) { \
                VNIC_TRACE( VNIC_DBG_WARN, (" %s (%d) cannot be greater than %s (%d)\n",#lo,lo,#hi,hi) ); \
                lo = hi; \
        }
#define RANGE_CHECK(x, min, max) if ((x < min) || (x > max)) { \
                VNIC_TRACE( VNIC_DBG_WARN, (" %s (%d) must be between %d and %d\n",#x,x,min,max) ); \
                if (x < min)  \
					x = min;  \
				else          \
					x = max;  \
        }
#define ZERO_RANGE_CHECK(x, min, max) if (x > max) { \
                VNIC_TRACE( VNIC_DBG_WARN, (" %s (%d) must be between %d and %d\n",#x,x,min,max) ); \
                x = max; \
        }

#define BOOLEAN_RANGE(x)  ZERO_RANGE_CHECK(x, 0, 1)
#define U32_ZERO_RANGE(x) ZERO_RANGE_CHECK(x, 0, 0x7FFFFFFF)
#define U32_RANGE(x)      RANGE_CHECK(x, 1, 0x7FFFFFFF)
#define U16_ZERO_RANGE(x) ZERO_RANGE_CHECK(x, 0, 0xFFFF)
#define U16_RANGE(x)      RANGE_CHECK(x, 1, 0xFFFF)
#define U8_ZERO_RANGE(x)  ZERO_RANGE_CHECK(x, 0, 0xFF)
#define U8_RANGE(x)       RANGE_CHECK(x, 1, 0xFF)

enum {
	INDEX_RESERVED,
	INDEX_MIN_HOST_POOL_SZ,
	INDEX_HOST_RECV_POOL_ENTRIES,
	INDEX_MIN_EIOC_POOL_SZ,
	INDEX_MAX_EIOC_POOL_SZ,
	INDEX_MIN_HOST_KICK_TIMEOUT,
	INDEX_MAX_HOST_KICK_TIMEOUT,
	INDEX_MIN_HOST_KICK_ENTRIES,
	INDEX_MAX_HOST_KICK_ENTRIES,
	INDEX_MIN_HOST_KICK_BYTES,
	INDEX_MAX_HOST_KICK_BYTES,
	INDEX_MIN_HOST_UPDATE_SZ,
	INDEX_MAX_HOST_UPDATE_SZ,
	INDEX_MIN_EIOC_UPDATE_SZ,
	INDEX_MAX_EIOC_UPDATE_SZ,
	INDEX_HEARTBEAT_TIMEOUT,
	INDEX_LAST // keep it the last entry
};

typedef struct _g_registry_params {
	PWSTR		name;
	uint32_t	value;
}g_registry_params_t;

#define ENTRY_INIT_VALUE			MAXULONG
#define VNIC_REGISTRY_TBL_SIZE		INDEX_LAST

typedef struct {
        uint64_t ioc_guid;
        uint64_t portGuid;
        uint64_t port;
        uint64_t hca;
        uint64_t instance;
        char  ioc_string[65];
        char  ioc_guid_set;
        char  ioc_string_set;
} PathParam_t;

typedef struct _vnic_globals {
	NDIS_HANDLE			ndis_handle; // ndis wrapper handle
	NDIS_SPIN_LOCK		lock;
#if ( LBFO_ENABLED )
	cl_qlist_t			primary_list;
	cl_qlist_t			secondary_list;
#endif
	uint32_t			shutdown;
	uint8_t				host_name[IB_NODE_DESCRIPTION_SIZE];
	g_registry_params_t	*p_params;
	cl_qlist_t			adapter_list;
} vnic_globals_t;

typedef struct IbConfig {
        ib_path_rec_t       pathInfo;
        uint64_t                 sid;
        Inic_ConnectionData_t connData;
        uint32_t                 retryCount;
        uint32_t                 rnrRetryCount;
        uint8_t                  minRnrTimer;
        uint32_t                 numSends;
        uint32_t                 numRecvs;
        uint32_t                 recvScatter; /* 1 */
        uint32_t                 sendGather;  /* 1 or 2 */
        uint32_t                 overrides;
} IbConfig_t;

typedef struct ControlConfig {
        IbConfig_t ibConfig;
        uint32_t      numRecvs;
        uint8_t       inicInstance;
        uint16_t      maxAddressEntries;
        uint16_t      minAddressEntries;
        uint32_t      rspTimeout;
        uint8_t       reqRetryCount;
        uint32_t      overrides;
} ControlConfig_t;

typedef struct DataConfig {
        IbConfig_t            ibConfig;
        uint64_t                 pathId;
        uint32_t                 numRecvs;
        uint32_t                 hostRecvPoolEntries;
        Inic_RecvPoolConfig_t hostMin;
        Inic_RecvPoolConfig_t hostMax;
        Inic_RecvPoolConfig_t eiocMin;
        Inic_RecvPoolConfig_t eiocMax;
        uint32_t                 notifyBundle;
        uint32_t                 overrides;
} DataConfig_t;

typedef struct ViportConfig {
        struct _viport  *pViport;
        ControlConfig_t controlConfig;
        DataConfig_t    dataConfig;
        uint32_t           hca;
        uint32_t           port;
        uint32_t           statsInterval;
        uint32_t           hbInterval; /* heartbeat interval */
        uint32_t           hbTimeout;  /* heartbeat timeout */
        uint64_t           portGuid;
        uint64_t           guid;
        size_t          instance;
        char            ioc_string[65];

#define HB_INTERVAL_OVERRIDE 0x1
#define GUID_OVERRIDE        0x2
#define STRING_OVERRIDE      0x4
#define HCA_OVERRIDE         0x8
#define PORT_OVERRIDE        0x10
#define PORTGUID_OVERRIDE    0x20
        uint32_t           overrides;
} ViportConfig_t;

/*
 * primaryConnectTimeout   - If the secondary connects first, how long do we
 *                         give the primary?
 * primaryReconnectTimeout - Same as above, but used when recovering when
 *                         both paths fail
 * primarySwitchTimeout    - How long do we wait before switching to the
 *                         primary when it comes back?
 */
#define IFNAMSIZ 65
typedef struct InicConfig {
        //struct Inic *pInic;
        char        name[IFNAMSIZ];
        uint32_t       noPathTimeout;
        uint32_t       primaryConnectTimeout;
        uint32_t       primaryReconnectTimeout;
        uint32_t       primarySwitchTimeout;
        int         preferPrimary;
        BOOLEAN     useRxCsum;
        BOOLEAN     useTxCsum;
#define USE_RX_CSUM_OVERRIDE 0x1
#define USE_TX_CSUM_OVERRIDE 0x2
        uint32_t       overrides;
} InicConfig_t;

typedef enum {
	INIC_UNINITIALIZED,
	INIC_DEREGISTERING,
	INIC_REGISTERED,
} InicState_t;

typedef enum {
	_ADAPTER_NOT_BUNDLED = 0,
	_ADAPTER_PRIMARY,
	_ADAPTER_SECONDARY
} lbfo_state_t;

#endif /* _VNIC_CONFIG_H_ */

