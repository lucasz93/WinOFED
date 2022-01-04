#pragma once

#define ETH_ADDR_LENGTH 6

NTSTATUS GetPortIpAddresses
(
    UCHAR         PortMac[ETH_ADDR_LENGTH],
    PVOID		  pIpAddresses,
    ULONG         *NumIpAddresses     
);

BOOLEAN IpBelongsToInterface
(
    UCHAR         PortMac[ETH_ADDR_LENGTH],
    PVOID         pAddress
);

BOOLEAN EqualIpAddresses(PSOCKADDR_INET p_addr1,
                             PSOCKADDR_INET p_addr2);

