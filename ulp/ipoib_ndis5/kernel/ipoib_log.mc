;/*++
;=============================================================================
;Copyright (c) 2001 Mellanox Technologies
;
;Module Name:
;
;    ipoiblog.mc
;
;Abstract:
;
;    IPoIB Driver event log messages
;
;Authors:
;
;    Yossi Leybovich
;
;Environment:
;
;   Kernel Mode .
;
;=============================================================================
;--*/
;
MessageIdTypedef = NDIS_ERROR_CODE

SeverityNames = (
	Success			= 0x0:STATUS_SEVERITY_SUCCESS
	Informational	= 0x1:STATUS_SEVERITY_INFORMATIONAL
	Warning			= 0x2:STATUS_SEVERITY_WARNING
	Error			= 0x3:STATUS_SEVERITY_ERROR
	)

FacilityNames = (
	System			= 0x0
	RpcRuntime		= 0x2:FACILITY_RPC_RUNTIME
	RpcStubs		= 0x3:FACILITY_RPC_STUBS
	Io				= 0x4:FACILITY_IO_ERROR_CODE
	IPoIB			= 0x7:FACILITY_IPOIB_ERROR_CODE
	)


MessageId=0x0001
Facility=IPoIB
Severity=Warning
SymbolicName=EVENT_IPOIB_PORT_DOWN
Language=English
%2: Network controller link is down.
.

MessageId=0x0002
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP
Language=English
%2: Network controller link is up.
.


MessageId=0x0003
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP1
Language=English
%2: Network controller link is up at 2.5Gbps.
.

MessageId=0x0004
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP2
Language=English
%2: Network controller link is up at 5Gbps.
.

MessageId=0x0006
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP3
Language=English
%2: Network controller link is up at 10Gbps.
.

MessageId=0x000a
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP4
Language=English
%2: Network controller link is up at 20Gps.
.

MessageId=0x000e
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP5
Language=English
%2: Network controller link is up at 30Gps.
.

MessageId=0x0012
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP6
Language=English
%2: Network controller link is up at 40Gps.
.

MessageId=0x001a
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP7
Language=English
%2: Network controller link is up at 60Gps.
.

MessageId=0x0032
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_PORT_UP8
Language=English
%2: Network controller link is up at 120Gps.
.

MessageId=0x0040
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_INIT_SUCCESS
Language=English
%2: Driver Initialized succesfully.
.

MessageId=0x0041
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_OPEN_CA
Language=English
%2: Failed to open Channel Adapter.
.

MessageId=0x0042
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_ALLOC_PD
Language=English
%2: Failed to allocate Protection Domain.
.

MessageId=0x0043
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_CREATE_RECV_CQ
Language=English
%2: Failed to create receive Completion Queue.
.

MessageId=0x0044
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_CREATE_SEND_CQ
Language=English
%2: Failed to create send Completion Queue.
.

MessageId=0x0045
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_CREATE_QP
Language=English
%2: Failed to create Queue Pair.
.

MessageId=0x0046
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_QUERY_QP
Language=English
%2: Failed to get Queue Pair number.
.

MessageId=0x0047
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_REG_PHYS
Language=English
%2: Failed to create DMA Memory Region.
.

MessageId=0x0048
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_RECV_POOL
Language=English
%2: Failed to create receive descriptor pool.
.

MessageId=0x0049
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_RECV_PKT_POOL
Language=English
%2: Failed to create NDIS_PACKET pool for receive indications.
.

MessageId=0x004A
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_RECV_BUF_POOL
Language=English
%2: Failed to create NDIS_BUFFER pool for receive indications.
.

MessageId=0x004B
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_SEND_PKT_POOL
Language=English
%2: Failed to create NDIS_PACKET pool for send processing.
.

MessageId=0x004C
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_SEND_BUF_POOL
Language=English
%2: Failed to create NDIS_BUFFER pool for send processing.
.

MessageId=0x004D
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_RECV_PKT_ARRAY
Language=English
%2: Failed to allocate receive indication array.
.

MessageId=0x004E
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_PORT_INFO_TIMEOUT
Language=English
%2: Subnet Administrator query for port information timed out. 
Make sure the SA is functioning properly.  Increasing the number
of retries and retry timeout adapter parameters may solve the
issue.
.

MessageId=0x004F
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_PORT_INFO_REJECT
Language=English
%2: Subnet Administrator failed the query for port information.
Make sure the SA is functioning properly and compatible.
.

MessageId=0x0050
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_QUERY_PORT_INFO
Language=English
%2: Subnet Administrator query for port information failed.
.

MessageId=0x0055
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_BCAST_GET
Language=English
%2: Subnet Administrator failed query for broadcast group information.
.

MessageId=0x0056
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_BCAST_JOIN
Language=English
%2: Subnet Administrator failed request to joing broadcast group.
.

MessageId=0x0057
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_BCAST_RATE
Language=English
%2: The local port rate is too slow for the existing broadcast MC group.
.

MessageId=0x0058
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_WRONG_PARAMETER_ERR
Language=English
%2: Incorrect value or non-existing registry  for the required IPoIB parameter %3, overriding it by default value: %4
.

MessageId=0x0059
Facility=IPoIB
Severity=Warning
SymbolicName=EVENT_IPOIB_WRONG_PARAMETER_WRN
Language=English
%2: Incorrect value or non-existing registry entry  for the required IPoIB parameter %3, overriding it by default value: %4
.

MessageId=0x005A
Facility=IPoIB
Severity=Informational
SymbolicName=EVENT_IPOIB_WRONG_PARAMETER_INFO
Language=English
%2: Incorrect value or non-existing registry  for the optional IPoIB parameter %3, overriding it by default value: %4
.

MessageId=0x005B
Facility=IPoIB
Severity=Error
SymbolicName=EVENT_IPOIB_PARTITION_ERR
Language=English
%2: Pkey index not found for partition , change switch pkey configuration.
.

