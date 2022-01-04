/*++
 
Copyright (c) 2005-2008 Mellanox Technologies. All rights reserved.

Module Name:
    offload.h

Abstract:
    Task offloading header file

Revision History:

Notes:

--*/

//
//  Define the maximum size of large TCP packets the driver can offload.
//  This sample driver uses shared memory to map the large packets, 
//  LARGE_SEND_OFFLOAD_SIZE is useless in this case, so we just define 
//  it as NIC_MAX_PACKET_SIZE. But shipping drivers should define
//  LARGE_SEND_OFFLOAD_SIZE if they support LSO, and use it as 
//  MaximumPhysicalMapping  when they call NdisMInitializeScatterGatherDma 
//  if they use ScatterGather method. If the drivers don't support
//  LSO, then MaximumPhysicalMapping is NIC_MAX_PACKET_SIZE.
//

#define LSO_MAX_HEADER 136
#define MAX_LSO_SIZE 60000 

// This struct is being used in order to pass data about the GSO buffers if they
// are present
struct LsoBuffer {
    PUCHAR pData;
    UINT Len;
};

struct LsoData {
    LsoBuffer LsoBuffers;
    UINT LsoHeaderSize;
    UCHAR coppied_data[LSO_MAX_HEADER];
};



