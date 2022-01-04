;/*++
;=============================================================================
;Copyright (c) 2009 Mellanox Technologies
;
;Module Name:
;
;    bus_ev_log.mc
;
;Abstract:
;
;    IB Driver event log messages
;
;Authors:
;
;    Leonid Keller
;
;Environment:
;
;   Kernel Mode .
;
;=============================================================================
;--*/
;
MessageIdTypedef = NTSTATUS

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
	IBBUS			= 0x9:FACILITY_IB_ERROR_CODE
	)


MessageId=0x0001 Facility=IBBUS Severity=Informational SymbolicName=EVENT_IBBUS_ANY_INFO
Language=English
%2
.

MessageId=0x0002 Facility=IBBUS Severity=Warning SymbolicName=EVENT_IBBUS_ANY_WARN
Language=English
%2
.

MessageId=0x0003 Facility=IBBUS Severity=Error SymbolicName=EVENT_IBBUS_ANY_ERROR
Language=English
%2
.

