;/*++
;=============================================================================
;Copyright (c) 2007 Mellanox Technologies
;
;Module Name:
;
;    ev_log.mc
;
;Abstract:
;
;    MLX4 Driver event log messages
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
	MLX4			= 0x8:FACILITY_MLX4_ERROR_CODE
	)


MessageId=0x0001 Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_ANY_INFO
Language=English
%2
.

MessageId=0x0002 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ANY_WARN
Language=English
%2
.
MessageId=0x0003 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ANY_ERROR
Language=English
%2
.

MessageId=0x0004 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_LIFEFISH_OK
Language=English
%2 has started in non-operational mode. 
.

MessageId=0x0006 Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_INFO_DEV_STARTED
Language=English
%2 has got:%n
   vendor_id      %t%3%n
   device_id      %t%4%n
   subvendor_id   %t%5%n
   subsystem_id   %t%6%n
   HW revision    %t%7%n
   FW version     %t%8.%9.%10%n
   HCA guid       %t%11:%12%n
   port number    %t%13%n
   req types      %t%14,%15%n
   final types    %t%16,%17
.

MessageId=0x0007 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_MAP_FA
Language=English
%2: MAP_FA command failed with error %3.%n 
The adapter card is non-functional.%n
Most likely a FW problem.%n
Please burn the last FW and restart the mlx4_bus driver.
.

MessageId=0x0008 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_RUN_FW
Language=English
%2: RUN_FW command failed with error %3.%n
The adapter card is non-functional.%n
Most likely a FW problem.%n
Please burn the last FW and restart the mlx4_bus driver.
.

MessageId=0x0009 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_QUERY_FW
Language=English
%2: QUERY_FW command failed with error %3.%n
The adapter card is non-functional.%n
Most likely a FW problem.%n
Please burn the last FW and restart the mlx4_bus driver.
.

MessageId=0x000a Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_QUERY_FW
Language=English
Found PF (non-primary physical function) at '%2'.
.

MessageId=0x000b Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_QUERY_DEV_CAP
Language=English
%2: QUERY_DEV_CAP command failed with error %3.%n
The adapter card is non-functional.%n
Most likely a FW problem.%n
Please burn the last FW and restart the mlx4_bus driver.
.

MessageId=0x000c Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_QUERY_ADAPTER
Language=English
QUERY_ADAPTER command failed with error %2.%n
The adapter card is non-functional.%n
Most likely a FW problem.%n
Please burn the last FW and restart the mlx4_bus driver.
.

MessageId=0x000d Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_NOT_ENOUGH_QPS
Language=English
Too few QPs were requested (requested %2, reserved for FW %3).%n
The adapter card is non-functional.%n
Please increase the Registry LogNumQp parameter under HKLM\System\CurrentControlSet\Services\mlx4_bus\Parameters.
.

MessageId=0x000e Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_INFO_RESET_START
Language=English
%2: Performing HCA restart ...
.

MessageId=0x000f Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_INFO_RESET_END
Language=English
%2: HCA restart finished. Notifying the clients ...
.

MessageId=0x0010 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_QUERY_FW_SRIOV
Language=English
Are you using SRIOV-enabled firmware?
.

MessageId=0x0011 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_LOCATION_MOVE
Language=English
Failed to move location string '%2', status %3.
.

MessageId=0x0012 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_GET_LOCATION
Language=English
WdfDeviceAllocAndQueryProperty failed,  status %2.
.

MessageId=0x0013 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_REG_ACTION
Language=English
%2 failed on %3 with status %4.
.

MessageId=0x0014 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_REG_OPEN_DEV_KEY
Language=English
WdfDeviceOpenRegistryKey failed on opening SW (=driver) key for mlx4_bus with status %2.
.

MessageId=0x0015 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_INVALID_PORT_TYPE_VALUE
Language=English
PortType registry parameter contains invalid value, PortType = %2.
.

MessageId=0x0016 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_PORT_TYPE_DPDP
Language=English
%2: Only same port types supported on this HCA.%n 
Please go to the Port Protocol UI, and change the port types to be Either as ETH or IB
.

MessageId=0x0017 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_PORT_TYPE_ETH_IB
Language=English
%2: Your port type configuration has led to not supported port types configuration (eth,ib).%n
If you have connected the ports to Ethernet and InfiniBand switches please replace between the ports. 
.

MessageId=0x0018 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ERROR_PORT_TYPE_SENSE_NOTHING
Language=English
%2:%n 
Problem - The port was configured to use auto sensing for deciding the port type. The port #%3 failed to detect the port type automatically.%n
Impact  - As a result the port is started as IB port (unless the port is ETH only). This may cause a connection problem if the other side is ETH port.%n
Reason and suggestion to fix - This problem may happen since the computer is connected back to back or the cable is unplugged.%n
For solving this issue connect the port to a switch or define the port type explicitly (IB or ETH) instead of auto.
.

MessageId=0x0019 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ERROR_PORT1_ETH_PORT2_TYPE_SENSE_NOTHING
Language=English
%2:%n 
Problem - The port was configured to use auto sensing for deciding the port type. The port #2 failed to detect the port type automatically.%n
Impact  - Since the first port is configured to be ETH the second port is started as ETH port.  This may cause a connection problem if the other side is IB port%n
Reason and suggestion to fix - This problem may happen since the computer is connected back to back or the cable is unplugged.%n
For solving this issue connect the port to a switch or define the port type explicitly (IB or ETH) instead of auto.
.

MessageId=0x001a Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_DEFERRED_ROCE_START
Language=English
%2: All ethernet drivers will be started after IBBUS driver, because %3 of them require RoCE. Ethernet drivers won't be started if IBBUS didn't start for some reason, e.g. mlx4_hca driver is disabled.
.

MessageId=0x001b Facility=MLX4 Severity=Informational SymbolicName=EVENT_MLX4_INFO_ROCE_START
Language=English
%2: Creating ethernet drivers ...
.

MessageId=0x001c Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ERROR_PORT1_ETH_PORT2_ROCE
Language=English
%2:%n 
Problem - The port was configured to use auto sensing to decide the port type.	Both ports were detected as Ethernet but RoCE was enabled only for port#2.%n
Thus creating an illegal ETH-RoCE port configuration. To resolve the problem, we have enabled RoCE on Port#1 as well.
.

MessageId=0x001d Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ERROR_PORT_TYPE_SENSE_CMD_FAILED
Language=English
%2:%n 
Problem - FW sense command could not be run on port#%3. We recommend upgrading your FW image. For further details, please refer to the README file in the documentation folder.
.


MessageId=0x001e Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_ERROR_PORT1_ETH_PORT1_TYPE_SENSE_NOTHING
Language=English
%2:%n 
Problem - The port was configured to use auto sensing for deciding the port type. The port #1 failed to detect the port type automatically.%n
Impact  - Since the second port is configured to be IB the first port is started as IB port.  This may cause a connection problem if the other side is ETH port%n
Reason and suggestion to fix - This problem may happen since the computer is connected back to back or the cable is unplugged.%n
For solving this issue connect the port to a switch or define the port type explicitly (IB or ETH) instead of auto.
.

MessageId=0x001f Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_BUF_ALLOC_FAILED
Language=English
Error, Allocating memory for device driver failed (memory size %3).%n
Either close any running applications, or reboot your computer or add additional memory.
.

MessageId=0x0020 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_BUF_ALLOC_FRAGMENTED
Language=English
Error, Allocating memory for device driver failed (memory size %3).%n
The miniport driver cannot start.%n
Either close any running applications, or reboot your computer or add additional memory.
.

MessageId=0x0021 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_MULTIFUNC_PORT_TYPE_CHANGED
Language=English
%2:%n 
Warning - IB configuration on Multi Protocol is prohibited. Forcing port#%3 to be ETH. 
.

MessageId=0x0022 Facility=MLX4 Severity=Error SymbolicName=EVENT_MLX4_ERROR_MULTIFUNC_PORT_TYPE_CONF_FAILED
Language=English
%2:%n 
Problem - On port#%3 device capabilities indicate IB only, which is not supported on a multi protocol machine. 
.

MessageId=0x0023 Facility=MLX4 Severity=Warning SymbolicName=EVENT_MLX4_WARN_MULTIFUNC_ROCE
Language=English
%2:%n 
Warning - RoCE configuration on Multi Protocol is prohibited. Forcing RoCE off.
.

