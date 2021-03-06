/*++

Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    GenUtils.h


Notes:

--*/


//
// Custom internal OIDs - This OIDs use internally for comunicate between the 
// MLNX intermidiate driver and the miniport driver and for query internal counters
// We don't want to expose it to customer, therefore it wasn't defined in custom_oids.h
//
#define OID_MLX_CUSTOM_INTERNAL_COUNTERS        0xFFA0C903
#define OID_MLX_ADD_VLAN_ID                     0xFFA0C905
#define OID_MLX_DELETE_VLAN_ID                  0xFFA0C906
#define OID_MLX_SET_MAC_ADDRESS                 0xFFA0C907
#define OID_MLX_CUSTOM_RESET                    0xFFA0C908
#define OID_GET_ADAPTERS_STATUS                 0xFFA0C909


inline NDIS_STATUS NtStatusToNdisStatus(NTSTATUS ntStatus)          
{    
    switch (ntStatus)
    {
        case STATUS_SUCCESS:
        case STATUS_PENDING:
        case STATUS_BUFFER_OVERFLOW:
        case STATUS_UNSUCCESSFUL:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_NOT_SUPPORTED:
            return (NDIS_STATUS)(ntStatus);                       

        case STATUS_BUFFER_TOO_SMALL:
            return NDIS_STATUS_BUFFER_TOO_SHORT;                   

        case STATUS_INVALID_BUFFER_SIZE:
            return NDIS_STATUS_INVALID_LENGTH;                     

        case STATUS_INVALID_PARAMETER:
            return NDIS_STATUS_INVALID_DATA;                       

        case STATUS_NO_MORE_ENTRIES:
            return NDIS_STATUS_ADAPTER_NOT_FOUND; 

        case STATUS_DEVICE_NOT_READY:
            return NDIS_STATUS_ADAPTER_NOT_READY;

        case STATUS_OBJECT_NAME_NOT_FOUND:
            return NDIS_STATUS_FILE_NOT_FOUND;

        default:
            return NDIS_STATUS_FAILURE;                            
    } 
}

inline LPSTR Oid2Str (NDIS_OID Oid) 
{
    switch(Oid) 
    {
        case OID_GEN_SUPPORTED_GUIDS : return "OID_GEN_SUPPORTED_GUIDS";
        case OID_802_11_BSSID : return "OID_802_11_BSSID";
        case OID_802_11_SSID : return "OID_802_11_SSID";
        case OID_802_11_NETWORK_TYPES_SUPPORTED : return "OID_802_11_NETWORK_TYPES_SUPPORTED";
        case OID_802_11_NETWORK_TYPE_IN_USE : return "OID_802_11_NETWORK_TYPE_IN_USE";
        case OID_802_11_TX_POWER_LEVEL : return "OID_802_11_TX_POWER_LEVEL";
        case OID_802_11_RSSI : return "OID_802_11_RSSI";
        case OID_802_11_RSSI_TRIGGER : return "OID_802_11_RSSI_TRIGGER";
        case OID_802_11_INFRASTRUCTURE_MODE : return "OID_802_11_INFRASTRUCTURE_MODE";
        case OID_802_11_FRAGMENTATION_THRESHOLD : return "OID_802_11_FRAGMENTATION_THRESHOLD";
        case OID_802_11_RTS_THRESHOLD : return "OID_802_11_RTS_THRESHOLD";
        case OID_802_11_NUMBER_OF_ANTENNAS : return "OID_802_11_NUMBER_OF_ANTENNAS";
        case OID_802_11_RX_ANTENNA_SELECTED : return "OID_802_11_RX_ANTENNA_SELECTED";
        case OID_802_11_TX_ANTENNA_SELECTED : return "OID_802_11_TX_ANTENNA_SELECTED";
        case OID_802_11_SUPPORTED_RATES : return "OID_802_11_SUPPORTED_RATES";
        case OID_802_11_DESIRED_RATES : return "OID_802_11_DESIRED_RATES";
        case OID_802_11_CONFIGURATION : return "OID_802_11_CONFIGURATION";
        case OID_802_11_STATISTICS : return "OID_802_11_STATISTICS";
        case OID_802_11_ADD_WEP : return "OID_802_11_ADD_WEP";
        case OID_802_11_REMOVE_WEP : return "OID_802_11_REMOVE_WEP";
        case OID_802_11_DISASSOCIATE : return "OID_802_11_DISASSOCIATE";
        case OID_802_11_POWER_MODE : return "OID_802_11_POWER_MODE";
        case OID_802_11_BSSID_LIST : return "OID_802_11_BSSID_LIST";
        case OID_802_11_AUTHENTICATION_MODE : return "OID_802_11_AUTHENTICATION_MODE";
        case OID_802_11_PRIVACY_FILTER : return "OID_802_11_PRIVACY_FILTER";
        case OID_802_11_BSSID_LIST_SCAN : return "OID_802_11_BSSID_LIST_SCAN";
        case OID_802_11_WEP_STATUS : return "OID_802_11_WEP_STATUS or OID_802_11_ENCRYPTION_STATUS"; 
        case OID_802_11_ADD_KEY : return "OID_802_11_ADD_KEY";
        case OID_802_11_REMOVE_KEY : return "OID_802_11_REMOVE_KEY";

        case OID_802_11_ASSOCIATION_INFORMATION : return "OID_802_11_ASSOCIATION_INFORMATION";
        case OID_802_11_TEST : return "OID_802_11_TEST";
        case OID_802_11_MEDIA_STREAM_MODE : return "OID_802_11_MEDIA_STREAM_MODE";
        case OID_802_11_RELOAD_DEFAULTS : return "OID_802_11_RELOAD_DEFAULTS";


        case OID_PNP_CAPABILITIES : return "OID_PNP_CAPABILITIES";
        case OID_PNP_SET_POWER : return "OID_PNP_SET_POWER";
        case OID_PNP_QUERY_POWER : return "OID_PNP_QUERY_POWER";
        case OID_PNP_ADD_WAKE_UP_PATTERN : return "OID_PNP_ADD_WAKE_UP_PATTERN";
        case OID_PNP_REMOVE_WAKE_UP_PATTERN : return "OID_PNP_REMOVE_WAKE_UP_PATTERN";
        case OID_PNP_WAKE_UP_PATTERN_LIST : return "OID_PNP_WAKE_UP_PATTERN_LIST";
        case OID_PNP_ENABLE_WAKE_UP : return "OID_PNP_ENABLE_WAKE_UP";

#define OID_802_11_CAPABILITY 0x0D010122
        case OID_802_11_CAPABILITY : return "OID_802_11_CAPABILITY";


        case OID_FFP_SUPPORT : return "OID_FFP_SUPPORT";
        case OID_FFP_FLUSH : return "OID_FFP_FLUSH";
        case OID_FFP_CONTROL : return "OID_FFP_CONTROL";
        case OID_FFP_PARAMS : return "OID_FFP_PARAMS";
        case OID_FFP_DATA : return "OID_FFP_DATA";
        case OID_FFP_DRIVER_STATS : return "OID_FFP_DRIVER_STATS";
        case OID_FFP_ADAPTER_STATS : return "OID_FFP_ADAPTER_STATS";
        case OID_GEN_XMIT_OK : return "OID_GEN_XMIT_OK";
        case OID_GEN_RCV_OK : return "OID_GEN_RCV_OK";
        case OID_GEN_XMIT_ERROR : return "OID_GEN_XMIT_ERROR";
        case OID_GEN_RCV_ERROR : return "OID_GEN_RCV_ERROR";
        case OID_GEN_RCV_NO_BUFFER : return "OID_GEN_RCV_NO_BUFFER";
        case OID_GEN_RCV_CRC_ERROR: return "OID_GEN_RCV_CRC_ERROR";


        case OID_802_3_PERMANENT_ADDRESS : return "OID_802_3_PERMANENT_ADDRESS";
        case OID_802_3_CURRENT_ADDRESS : return "OID_802_3_CURRENT_ADDRESS";
        case OID_802_3_MULTICAST_LIST : return "OID_802_3_MULTICAST_LIST";
        case OID_802_3_MAXIMUM_LIST_SIZE : return "OID_802_3_MAXIMUM_LIST_SIZE";
        case OID_802_3_MAC_OPTIONS : return "OID_802_3_MAC_OPTIONS";
        case NDIS_802_3_MAC_OPTION_PRIORITY : return "NDIS_802_3_MAC_OPTION_PRIORITY";
        case OID_802_3_RCV_ERROR_ALIGNMENT : return "OID_802_3_RCV_ERROR_ALIGNMENT";
        case OID_802_3_XMIT_ONE_COLLISION : return "OID_802_3_XMIT_ONE_COLLISION";
        case OID_802_3_XMIT_MORE_COLLISIONS : return "OID_802_3_XMIT_MORE_COLLISIONS";
        case OID_802_3_XMIT_DEFERRED : return "OID_802_3_XMIT_DEFERRED";
        case OID_802_3_XMIT_MAX_COLLISIONS : return "OID_802_3_XMIT_MAX_COLLISIONS";
        case OID_802_3_RCV_OVERRUN : return "OID_802_3_RCV_OVERRUN";
        case OID_802_3_XMIT_UNDERRUN : return "OID_802_3_XMIT_UNDERRUN";
        case OID_802_3_XMIT_HEARTBEAT_FAILURE : return "OID_802_3_XMIT_HEARTBEAT_FAILURE";
        case OID_802_3_XMIT_TIMES_CRS_LOST : return "OID_802_3_XMIT_TIMES_CRS_LOST";
        case OID_802_3_XMIT_LATE_COLLISIONS : return "OID_802_3_XMIT_LATE_COLLISIONS";

        case OID_GEN_SUPPORTED_LIST : return "OID_GEN_SUPPORTED_LIST";
        case OID_GEN_HARDWARE_STATUS : return "OID_GEN_HARDWARE_STATUS";
        case OID_GEN_MEDIA_SUPPORTED : return "OID_GEN_MEDIA_SUPPORTED";
        case OID_GEN_MEDIA_IN_USE : return "OID_GEN_MEDIA_IN_USE";
        case OID_GEN_MAXIMUM_LOOKAHEAD : return "OID_GEN_MAXIMUM_LOOKAHEAD";
        case OID_GEN_MAXIMUM_FRAME_SIZE : return "OID_GEN_MAXIMUM_FRAME_SIZE";
        case OID_GEN_LINK_SPEED : return "OID_GEN_LINK_SPEED";
        case OID_GEN_TRANSMIT_BUFFER_SPACE : return "OID_GEN_TRANSMIT_BUFFER_SPACE";
        case OID_GEN_RECEIVE_BUFFER_SPACE : return "OID_GEN_RECEIVE_BUFFER_SPACE";
        case OID_GEN_TRANSMIT_BLOCK_SIZE : return "OID_GEN_TRANSMIT_BLOCK_SIZE";
        case OID_GEN_RECEIVE_BLOCK_SIZE : return "OID_GEN_RECEIVE_BLOCK_SIZE";
        case OID_GEN_VENDOR_ID : return "OID_GEN_VENDOR_ID";
        case OID_GEN_VENDOR_DESCRIPTION : return "OID_GEN_VENDOR_DESCRIPTION";
        case OID_GEN_CURRENT_PACKET_FILTER : return "OID_GEN_CURRENT_PACKET_FILTER";
        case OID_GEN_CURRENT_LOOKAHEAD : return "OID_GEN_CURRENT_LOOKAHEAD";
        case OID_GEN_DRIVER_VERSION : return "OID_GEN_DRIVER_VERSION";
        case OID_GEN_MAXIMUM_TOTAL_SIZE : return "OID_GEN_MAXIMUM_TOTAL_SIZE";
        case OID_GEN_PROTOCOL_OPTIONS : return "OID_GEN_PROTOCOL_OPTIONS";
        case OID_GEN_MAC_OPTIONS : return "OID_GEN_MAC_OPTIONS";
        case OID_GEN_MEDIA_CONNECT_STATUS : return "OID_GEN_MEDIA_CONNECT_STATUS";
        case OID_GEN_MAXIMUM_SEND_PACKETS : return "OID_GEN_MAXIMUM_SEND_PACKETS";
        
        case OID_TCP_TASK_OFFLOAD : return "OID_TCP_TASK_OFFLOAD";
        case OID_TCP_TASK_IPSEC_ADD_SA : return "OID_TCP_TASK_IPSEC_ADD_SA";
        case OID_TCP_TASK_IPSEC_DELETE_SA : return "OID_TCP_TASK_IPSEC_DELETE_SA";
        case OID_TCP_SAN_SUPPORT : return "OID_TCP_SAN_SUPPORT";
        case OID_TCP_TASK_IPSEC_ADD_UDPESP_SA : return "OID_TCP_TASK_IPSEC_ADD_UDPESP_SA";
        case OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA : return "OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA";
        case OID_TCP4_OFFLOAD_STATS : return "OID_TCP4_OFFLOAD_STATS";
        case OID_TCP6_OFFLOAD_STATS : return "OID_TCP6_OFFLOAD_STATS";
        case OID_IP4_OFFLOAD_STATS : return "OID_IP4_OFFLOAD_STATS";
        case OID_IP6_OFFLOAD_STATS : return "OID_IP6_OFFLOAD_STATS";


        case OID_GEN_VENDOR_DRIVER_VERSION : return "OID_GEN_VENDOR_DRIVER_VERSION";
        case OID_GEN_NETWORK_LAYER_ADDRESSES : return "OID_GEN_NETWORK_LAYER_ADDRESSES";
        case OID_GEN_TRANSPORT_HEADER_OFFSET : return "OID_GEN_TRANSPORT_HEADER_OFFSET";
        case OID_GEN_MACHINE_NAME : return "OID_GEN_MACHINE_NAME";
        case OID_GEN_RNDIS_CONFIG_PARAMETER : return "OID_GEN_RNDIS_CONFIG_PARAMETER";
        case OID_GEN_VLAN_ID : return "OID_GEN_VLAN_ID";
        case OID_GEN_MEDIA_CAPABILITIES : return "OID_GEN_MEDIA_CAPABILITIES";
        case OID_GEN_PHYSICAL_MEDIUM : return "OID_GEN_PHYSICAL_MEDIUM";
        case OID_GEN_RECEIVE_SCALE_CAPABILITIES : return "OID_GEN_RECEIVE_SCALE_CAPABILITIES";
        case OID_GEN_RECEIVE_SCALE_PARAMETERS : return "OID_GEN_RECEIVE_SCALE_PARAMETERS";
        case OID_GEN_RECEIVE_HASH : return "OID_GEN_RECEIVE_HASH";
        case OID_GEN_TIMEOUT_DPC_REQUEST_CAPABILITIES : return "OID_GEN_TIMEOUT_DPC_REQUEST_CAPABILITIES";

        case OID_MLX_ADD_VLAN_ID  :                 return "OID_MLX_ADD_VLAN_ID";
        case OID_MLX_DELETE_VLAN_ID:              return "OID_MLX_DELETE_VLAN_ID";
        case OID_MLX_SET_MAC_ADDRESS:               return "OID_MLX_SET_MAC_ADDRESS";
        case OID_GET_ADAPTERS_STATUS:           return "OID_GET_ADAPTERS_STATUS";
            
        
#ifdef NDIS_SUPPORT_NDIS6 
        //
        // NDIS 6.0 OIDs
        //
        case OID_TCP_OFFLOAD_CURRENT_CONFIG: return "OID_TCP_OFFLOAD_CURRENT_CONFIG";
        case OID_TCP_OFFLOAD_PARAMETERS: return "OID_TCP_OFFLOAD_PARAMETERS";
        case OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES: return "OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES";
        case OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG: return "OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG";
        case OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES: return "OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES";
        case OID_OFFLOAD_ENCAPSULATION: return "OID_OFFLOAD_ENCAPSULATION";
        case OID_GEN_STATISTICS: return "OID_GEN_STATISTICS";
        case OID_GEN_INTERRUPT_MODERATION: return "OID_GEN_INTERRUPT_MODERATION";
#endif
    }
    return "UnKnown";

}


