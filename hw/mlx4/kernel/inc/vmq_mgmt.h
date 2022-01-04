
#pragma once

#if NDIS_SUPPORT_NDIS620 

#define GET_VFILTERS_FOR_ALL_VMQS               0x0000FFFF
#define NDIS_MLNX_VQUEUE_INFO_REVISION_1        1
#define NDIS_MLNX_VFILTER_PARAMETERS_REVISION_1 1

typedef struct _MP_PORT MP_PORT, *PMP_PORT;

typedef struct _MLNX_VQUEUE_INFO_ARRAY {
  NDIS_OBJECT_HEADER Header;
  ULONG              FirstElementOffset;
  ULONG              NumElements;
  ULONG              ElementSize;
} MLNX_VQUEUE_INFO_ARRAY, *PMLNX_VQUEUE_INFO_ARRAY;

enum MLNX_VQUEUE_CREATE_TYPE
{
    MLNX_VQUEUE_CREATE_TYPE_VMQ,
    MLNX_VQUEUE_CREATE_TYPE_NON_VMQ
};

enum MLNX_VQUEUE_TYPE
{
    MLNX_VQUEUE_TYPE_RX,
    MLNX_VQUEUE_TYPE_TX,
    MLNX_VQUEUE_TYPE_BOTH
};

enum MLNX_VQUEUE_PHY_TYPE
{
    MLNX_VQUEUE_PHY_TYPE_IPOIB,
    MLNX_VQUEUE_PHY_TYPE_ETH
};

typedef struct _MLNX_VQUEUE_RECEIVE_INFO
{
    ULONG                                NumReceiveBuffers;
    ULONG                                LookaheadSize;
} MLNX_VQUEUE_RECEIVE_INFO;

typedef struct _MLNX_VQUEUE_TRANSMIT_INFO
{
    ULONG                                NumTransmitBuffers;
} MLNX_VQUEUE_TRANSMIT_INFO;

typedef ULONG NDIS_RECEIVE_QUEUE_ID, *PNDIS_RECEIVE_QUEUE_ID;

typedef struct _MLNX_VQUEUE_INFO {
  NDIS_OBJECT_HEADER                   Header;
  ULONG                                Flags;
  MLNX_VQUEUE_PHY_TYPE                 VQueuePhyType;       // IPoIB or Ethernet
  MLNX_VQUEUE_CREATE_TYPE              CreateVQueueType;    // the trigger for queue creation - VMQ or non-VMQ
  MLNX_VQUEUE_TYPE                     VQueueType;          // queue usage - RX, TX or both
  NDIS_RECEIVE_QUEUE_ID                QueueId;
  NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE QueueState;
  GROUP_AFFINITY                       ProcessorAffinity;
  WCHAR                                VmName[IF_MAX_STRING_SIZE + 1];
  WCHAR                                QueueName[257];
  ULONG                                NumFilters;
  MLNX_VQUEUE_RECEIVE_INFO             RecvInfo; // Relevant only for RX queue type
  MLNX_VQUEUE_TRANSMIT_INFO            TxInfo; // Relevant only for  TX queue type
} MLNX_VQUEUE_INFO, *PMLNX_VQUEUE_INFO;

typedef struct _MLNX_VFILTER_INFO_ARRAY {
  NDIS_OBJECT_HEADER       Header;
  NDIS_RECEIVE_QUEUE_ID    QueueId; // GET_VFILTERS_FOR_ALL_VMQS For all
  ULONG                    FirstElementOffset;
  ULONG                    NumElements;
  ULONG                    ElementSize;
  ULONG                    Flags;
} MLNX_VFILTER_INFO_ARRAY, *PMLNX_VFILTER_INFO_ARRAY;

typedef struct _MLNX_QP_INFO {
  MLNX_VQUEUE_TYPE          QpType; // QP usage - RX, TX or both
  ULONG                     PortNum;
  ULONG                     QPn;
  ULONG                     SRQn;
  ULONG                     RxCQn;
  ULONG                     RxEQn;
  ULONG                     TxCQn;
  ULONG                     TxEQn;
} MLNX_QP_INFO;

typedef ULONG NDIS_RECEIVE_FILTER_ID, *PNDIS_RECEIVE_FILTER_ID;

typedef struct _MLNX_VFILTER_PARAMETERS {
  NDIS_OBJECT_HEADER       Header;
  ULONG                    Flags;
  NDIS_RECEIVE_QUEUE_ID    QueueId;
  NDIS_RECEIVE_FILTER_ID   FilterId;
/*  ULONG64                  MacAddress;
  bool                     VlanIdValid;
  ULONG                    VlanId;*/
  MLNX_QP_INFO             RxQpInfo;
  MLNX_QP_INFO             TxQpInfo;
  ULONG                    FieldParametersArrayOffset; // array of NDIS_RECEIVE_FILTER_FIELD_PARAMETERS that define the filter
  ULONG                    FieldParametersArrayNumElements;
  ULONG                    FieldParametersArrayElementSize;
} MLNX_VFILTER_PARAMETERS, *PMLNX_VFILTER_PARAMETERS;

ULONG
GetNumOfVqueue(
    PMP_PORT pPort
    );

ULONG
GetVqueueSize(
    IN PMP_PORT pPort, 
    IN PVOID InformationBuffer
    );

NDIS_STATUS
GetVqueueStats(
    IN PMP_PORT pPort, 
    IN PVOID InformationBuffer,
    OUT PVOID *ppInfo
    );

ULONG
GetNumOfVfilterPerVmq(
    PMP_PORT pPort, 
    ULONG QueueId
    );

ULONG
GetVfilterSize(
    IN PMP_PORT pPort, 
    IN PVOID InformationBuffer
    );

NDIS_STATUS
GetVfilterStats(
    PMP_PORT pPort,
    PVOID InformationBuffer,
    PVOID *ppInfo
    );
#endif
