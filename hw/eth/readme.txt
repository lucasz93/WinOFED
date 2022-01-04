
This directory is temporary.
It contains binaries of ethernet driver which eventually will be provided on Mellanox site.
To transfer HCA ports to RoCE mode one has to do the following manual procedure:\

1. Install OFED package;

2. Run regedit, find in CurrentControlSet "PortType".
You are to see two parameters:
EnableRoce 0,0
PortType   auto,auto

Change them for the necessary port configuration.

PortType describes the transport type of the port.
Possible values:
  ib - Infiniband
  eth - Ethernet
  auto - autosense (relevant only when HCA  is connected to a switch)

EnableRoce is relevant only for Ethernet ports.
When set to 1 it enables RoCE over this port.

HW limitation: If Port1 is Ethernet, Port2 must be also be Ethernet.

Here are examples of possible configurations:

IB:IB
EnableRoce 0,0Ether
PortType   ib,ib

Eth:Eth
EnableRoce 0,0
PortType   eth,eth

IB:RoCE
EnableRoce 0,1
PortType   ib,eth

RoCE:RoCE
EnableRoce 1,1
PortType   eth,eth

RoCE:IB, ETH:IB
Forbidden

3.Restart bus device to make it reconfigure ports
The bus device is found in DeviceManager under SystemDevices.

4.If you made one of the ports Eth or RoCE, OS will ask you for Ethernet driver.
Copy the installatioin files of the driver from branches\mlx4_full\hw\eth to a local folder and provide it to OS.
