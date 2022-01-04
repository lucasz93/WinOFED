



typedef int  (*MLX4_ADD_ETH) (struct _FDO_DEVICE_DATA*  p_fdo);

typedef struct _MLX4_BXM_INTERFACE{
	INTERFACE i;
    
    MLX4_ADD_ETH     mlx4_add_eth;    
	struct _FDO_DEVICE_DATA* p_fdo;
} MLX4_BXM_INTERFACE, *PMLX4_BXM_INTERFACE;


// {FD144907-C431-4dd1-939F-FE5CCB2FEAEF}
DEFINE_GUID(MLX4_BXM_INTERFACE_GUID, 
0xfd144907, 0xc431, 0x4dd1, 0x93, 0x9f, 0xfe, 0x5c, 0xcb, 0x2f, 0xea, 0xef);

