#include "gu_precomp.h"

NTSTATUS GetPortIpAddresses
(
    UCHAR         PortMac[ETH_ADDR_LENGTH],
    PVOID         pIpAddressesBuf,
    ULONG         *NumIpAddresses     
)
{
    NTSTATUS status;    
    SOCKADDR_INET *pIpAddresses = (SOCKADDR_INET *) pIpAddressesBuf;
    PMIB_UNICASTIPADDRESS_TABLE pUcastIpTable = NULL;
    ULONG i, j, NumEntries = 0;
    PMIB_UNICASTIPADDRESS_ROW pUcastIpRow = NULL;
    PMIB_IF_TABLE2 pIfTable = NULL;
    PMIB_IF_ROW2 pIfRow = NULL;

    if(NumIpAddresses == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if(pIpAddresses == NULL && *NumIpAddresses != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
     
    status = GetIfTable2Ex(MibIfTableNormal, &pIfTable);

    if( !NT_SUCCESS(status) )
        return status;

    for(i = 0; i < pIfTable->NumEntries; i++)
    {
        pIfRow = &pIfTable->Table[i];

        if( ! pIfRow->InterfaceAndOperStatusFlags.FilterInterface &&
            pIfRow->PhysicalAddressLength == ETH_ADDR_LENGTH &&
            pIfRow->MediaConnectState == MediaConnectStateConnected &&
            ! memcmp(PortMac, pIfRow->PhysicalAddress, ETH_ADDR_LENGTH) )
        {
            break;
        }        
    }

    if(i == pIfTable->NumEntries)
    {// interface for given MAC not found
        return STATUS_NOT_FOUND;
    }
    
	status = GetUnicastIpAddressTable(AF_UNSPEC, &pUcastIpTable);

    if( !NT_SUCCESS(status) )
        return status;

    for(i = 0; i < pUcastIpTable->NumEntries; i++)
    {
        pUcastIpRow = &pUcastIpTable->Table[i];

        if( pUcastIpRow->InterfaceLuid.Value == pIfRow->InterfaceLuid.Value )
        {
            NumEntries++;
        }
    }

    if ( NumEntries == 0 )
    {
        *NumIpAddresses = NumEntries;
        status = STATUS_NOT_FOUND;
        goto exit;
    }

    if ( *NumIpAddresses < NumEntries )
    {
        *NumIpAddresses = NumEntries;
        status = STATUS_BUFFER_TOO_SMALL;
        goto exit;
    }

    *NumIpAddresses = NumEntries;
    
    j = 0;
    
    for(i = 0; i < pUcastIpTable->NumEntries; i++)
    {
        pUcastIpRow = &pUcastIpTable->Table[i];

        if( pUcastIpRow->InterfaceLuid.Value == pIfRow->InterfaceLuid.Value )
        {
            RtlCopyMemory(&pIpAddresses[j++], &pUcastIpRow->Address, sizeof(SOCKADDR_INET));
        }
    }
    
exit:   
    if(pUcastIpTable)
    {
        FreeMibTable(pUcastIpTable);
    }
    if(pIfTable)
    {
        FreeMibTable(pIfTable);
    }
    return status;
}

BOOLEAN IpBelongsToInterface
(
    UCHAR         PortMac[ETH_ADDR_LENGTH],
    PVOID         pAddress
)
{
    NTSTATUS status;    
    SOCKADDR_INET *pIpAddress = (SOCKADDR_INET *) pAddress;
    PMIB_UNICASTIPADDRESS_TABLE pUcastIpTable = NULL;
    ULONG i;
    PMIB_UNICASTIPADDRESS_ROW pUcastIpRow = NULL;
    PMIB_IF_TABLE2 pIfTable = NULL;
    PMIB_IF_ROW2 pIfRow = NULL;
    BOOLEAN retVal = FALSE;
    
    if(pAddress == NULL)
    {
        return FALSE;
    }
     
    status = GetIfTable2Ex(MibIfTableNormal, &pIfTable);

    if( !NT_SUCCESS(status) )
        return FALSE;

    for(i = 0; i < pIfTable->NumEntries; i++)
    {
        pIfRow = &pIfTable->Table[i];

        if( ! pIfRow->InterfaceAndOperStatusFlags.FilterInterface &&
            pIfRow->PhysicalAddressLength == ETH_ADDR_LENGTH &&
            pIfRow->MediaConnectState == MediaConnectStateConnected &&
            ! memcmp(PortMac, pIfRow->PhysicalAddress, ETH_ADDR_LENGTH) )
        {
            break;
        }        
    }

    if(i == pIfTable->NumEntries)
    {// interface for given MAC not found
        retVal = FALSE;
        goto exit;
    }
    
	status = GetUnicastIpAddressTable(pIpAddress->si_family, &pUcastIpTable);

    if( !NT_SUCCESS(status) )
    {
        retVal = FALSE;
        goto exit;
    }
    
    for(i = 0; i < pUcastIpTable->NumEntries; i++)
    {
        pUcastIpRow = &pUcastIpTable->Table[i];

        if( pUcastIpRow->InterfaceLuid.Value == pIfRow->InterfaceLuid.Value &&
            EqualIpAddresses(&pUcastIpRow->Address, pIpAddress))
        {
            retVal = TRUE;
            break;
        }
    }

    if ( i == pUcastIpTable->NumEntries )
    {
        retVal = FALSE;
        goto exit;
    }
    
exit:   
    if(pUcastIpTable)
    {
        FreeMibTable(pUcastIpTable);
    }
    if(pIfTable)
    {
        FreeMibTable(pIfTable);
    }
    return retVal;
}

BOOLEAN EqualIpAddresses(PSOCKADDR_INET p_addr1,
                             PSOCKADDR_INET p_addr2)
{
    if(p_addr1->si_family != p_addr2->si_family)
    {
        return FALSE;
    }
    
    if( p_addr1->si_family == AF_INET )
    {// IPv4
        if(((PSOCKADDR_IN) p_addr1)->sin_addr.s_addr ==
           ((PSOCKADDR_IN) p_addr2)->sin_addr.s_addr) 
        {
            return TRUE;
        }
    }
    else
    {// IPv6
        if(IN6_ADDR_EQUAL(
                &((PSOCKADDR_IN6) p_addr1)->sin6_addr,
                &((PSOCKADDR_IN6) p_addr2)->sin6_addr)) 
        {
            return TRUE;
        }
    }
    return FALSE;
}

