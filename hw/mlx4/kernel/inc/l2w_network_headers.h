#pragma once

#define ETH_ALEN        6   /* MAC address length in bytes */
#define ETH_HLEN        14  /* MAC header length in bytes */

#pragma pack(push, 1)

struct ethhdr 
{
         unsigned char   h_dest[ETH_ALEN];       /* destination MAC */
         unsigned char   h_source[ETH_ALEN];     /* source MAC    */
         unsigned short  h_proto;                /* next protocol type */
};

#pragma pack(pop)

