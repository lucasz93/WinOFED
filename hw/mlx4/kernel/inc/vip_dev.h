/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:
    mtnic_dev.h

Abstract:
    define the essential structure that is common to mxe_bus and the connectx driver
    
Revision History:

Notes:

--*/

#pragma once

#define MTNIC_MAX_PORTS     2
#define MAX_MSIX_VECTORES   128
#define VLAN_FLTR_SIZE	    128

#define MXE_INTERFACE_VERSION	2

enum mtnic_state {
	CARD_DOWN,
	CARD_UP,
	CARD_GOING_DOWN,
	CARD_DISABLED
};

struct _MP_PORT;

struct mlx4_en_eq_info {
    struct VipBusIfc * pVipBusIfc;
    struct mlx4_eq* eq;
    u8 eq_number;
    BOOLEAN fUseMsix;
};

typedef struct {
    enum mtnic_state     state;
    KEVENT          ConfigChangeEvent; 

    // Objects that are needed in order to work with the hw

    u32                     priv_pdn;
    struct mlx4_uar         priv_uar;
    void __iomem            *uar_map;
    struct mlx4_mr          mr;
    spinlock_t*             puar_lock;

    BOOLEAN                 use_msix;
    u8                      n_valid_eq;
    struct mlx4_en_eq_info  eq_info[MAX_MSIX_VECTORES];
} NicData_t;

/* This structure is a part of MLX4_BUS_IB_INTERFACE */
/* Upon its changing you have to change MLX4_BUS_IB_INTERFACE_VERSION */
struct VipBusIfc
{
    PVOID           Context;
    LONG            NoOfConnectedPorts;

    NicData_t       NicData;

    struct _MP_PORT *ports[MTNIC_MAX_PORTS];
    
};



