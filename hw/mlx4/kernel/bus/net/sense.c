#include "mlx4.h"
#include "mlx4_debug.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "sense.tmh"
#endif


static int mlx4_SENSE_PORT(struct mlx4_dev *dev, int port,
			   enum mlx4_port_type *type)
{
	u64 out_param;
	int err = 0;

	err = mlx4_cmd_imm(dev, 0, &out_param, port, 0,
			   MLX4_CMD_SENSE_PORT, MLX4_CMD_TIME_CLASS_B);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Sense command failed for port: %d\n", dev->pdev->name, port));
		return err;
	}

	if (out_param > 2) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Sense returned illegal value: 0x%I64x\n", dev->pdev->name, out_param));
		return -EINVAL;
	}

	*type = (mlx4_port_type)out_param;
	return 0;
}


void mlx4_do_sense_ports(struct mlx4_dev *dev,
			 enum mlx4_port_type *stype,
			 enum mlx4_port_type *defaults)
{
	struct mlx4_sense *sense = &mlx4_priv(dev)->sense;
	int err;
	int i;

	for (i = 1; i <= dev->caps.num_ports; i++) 
	{
		stype[i - 1] = (mlx4_port_type)0;
		sense->sense_results[i] = 0xff;		// for post mortem
		if (sense->sense_allowed[i])
		{
			err = mlx4_SENSE_PORT(dev, i, &stype[i - 1]);
            sense->sense_results[i] = (err) ? (u8)err : (u8)stype[i - 1];
            if(err)
            {
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Sense for port %d failed.err=0x%x\n",dev->pdev->name,i,err));
                WriteEventLogEntryData(dev->pdev->p_self_do, 
                                       (ULONG)EVENT_MLX4_ERROR_PORT_TYPE_SENSE_CMD_FAILED,
                                       0, 0, 2, L"%S",dev->pdev->name,L"%d",i);
                
            }
            else if(stype[i-1] == 0)
            {
                
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Sense nothing for port #%d.\n ", dev->pdev->name,i));
                WriteEventLogEntryData(dev->pdev->p_self_do, 
                                       (ULONG)EVENT_MLX4_ERROR_PORT_TYPE_SENSE_NOTHING,
                                       0, 0, 2, L"%S",dev->pdev->name,L"%d",i);
                
            }                                 			
		}
		else
		{
			stype[i - 1] = defaults[i - 1];
		}
	}

    //
    // Adjust port configuration:
    // If port 1 sensed nothing and port 2 is IB, set both as IB
    // If port 2 sensed nothing and port 1 is Eth, set both as Eth
    // 
	if (stype[1] == MLX4_PORT_TYPE_IB && stype[0] == 0)
	{        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Sense for port 1 failed. Since the second port is IB set the first port to be IB.\n", dev->pdev->name));
        WriteEventLogEntryData(dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_PORT1_ETH_PORT1_TYPE_SENSE_NOTHING,0, 0, 1, L"%S",dev->pdev->name);
		stype[0] = MLX4_PORT_TYPE_IB;
	}
    
	if (sense->sense_allowed[2] && stype[0] == MLX4_PORT_TYPE_ETH && stype[1] == 0) 
	{
        
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Sense for port 2 failed. Since the first port is ETH set the second port to be ETH.\n", dev->pdev->name));
        WriteEventLogEntryData(dev->pdev->p_self_do,(ULONG)EVENT_MLX4_ERROR_PORT1_ETH_PORT2_TYPE_SENSE_NOTHING,0, 0, 1, L"%S",dev->pdev->name);
        stype[1] = MLX4_PORT_TYPE_ETH;
	}
    
    //
    // If sensed nothing, remain in default configuration.
    //
    for (i = 0; i < dev->caps.num_ports; i++)
    {
        if(stype[i] == 0)
        {
            stype[i] = defaults[i];
        }
    }

    if (stype[0] == MLX4_PORT_TYPE_ETH && stype[1] == MLX4_PORT_TYPE_ETH && 
        !dev->dev_params.enable_roce[1] && dev->dev_params.enable_roce[2])
    {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Prohibited configuration ETH-RoCE. Forcing Port1 to be RoCE\n ", dev->pdev->name));
        WriteEventLogEntryData(dev->pdev->p_self_do, 
                               (ULONG)EVENT_MLX4_ERROR_PORT1_ETH_PORT2_ROCE,
                               0, 0, 1, L"%S",dev->pdev->name);
        dev->dev_params.enable_roce[1] = 1;
    }
    
    //
    // Remove RoCE from IB ports
    //
    for (i = 0; i < dev->caps.num_ports; i++) 
    {
        if (stype[i] == MLX4_PORT_TYPE_IB) 
        {
            dev->dev_params.enable_roce[i+1] = 0;
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                            ("%s: IB port %d has RoCE set. Remove this setting\n ", dev->pdev->name, i+1));
        }
    }
}

int mlx4_adjust_mfunc_ports(struct mlx4_dev *dev)
{
    int i = 0;
    
    if (!mlx4_is_mfunc(dev))
    {
        return 0;
    }

    for (i = 1; i <= dev->caps.num_ports; i++) 
    {
        if(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_IB)
        {
            if (dev->caps.port_types_cap[i] & MLX4_PORT_TYPE_ETH)
            {
                MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: IB on Multi Protocol is prohibited. Forcing Port#%d to be ETH\n", dev->pdev->name, i));
                WriteEventLogEntryData(dev->pdev->p_self_do, 
                               (ULONG)EVENT_MLX4_WARN_MULTIFUNC_PORT_TYPE_CHANGED,
                               0, 0, 2, L"%S",dev->pdev->name,L"%d",i);  
                dev->caps.port_type_final[i] = MLX4_PORT_TYPE_ETH;
            }
            else
            {
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: IB on Multi Protocol is prohibited. "
                    "No valid configuration exists\n", dev->pdev->name));
                WriteEventLogEntryData(dev->pdev->p_self_do, 
                                   (ULONG)EVENT_MLX4_ERROR_MULTIFUNC_PORT_TYPE_CONF_FAILED,
                                   0, 0, 2, L"%S",dev->pdev->name,L"%d",i);  
                return -ENODEV;
            }
        }
        else
        {
            ASSERT(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_ETH); // We should have a final configuration
            if (mlx4_is_mfunc(dev) && 
                (dev->dev_params.enable_roce[1] || dev->dev_params.enable_roce[2]))
            {
                MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("%s: RoCE configuration  on Multi Protocol is prohibited. Forcing RoCE off.\n ", dev->pdev->name));
                WriteEventLogEntryData(dev->pdev->p_self_do, 
                                       (ULONG)EVENT_MLX4_WARN_MULTIFUNC_ROCE,
                                       0, 0, 1, L"%S",dev->pdev->name);
                dev->dev_params.enable_roce[1] = 0;
                dev->dev_params.enable_roce[2] = 0;
            }
        }
    }

    return 0;
    
}

int mlx4_sense_port(struct mlx4_dev *dev)
{
    int i;
    enum mlx4_port_type stype[MLX4_MAX_PORTS];
    enum mlx4_port_type sdefaults[MLX4_MAX_PORTS];

    for (i = 1; i <= dev->caps.num_ports; i++) 
    {
        if(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_ETH)
        {
            sdefaults[i-1] = MLX4_PORT_TYPE_ETH;
            ASSERT(mlx4_priv(dev)->sense.sense_allowed[i] == 0);
        }
        else if(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_IB)
        {
            sdefaults[i-1] = MLX4_PORT_TYPE_IB;
            ASSERT(mlx4_priv(dev)->sense.sense_allowed[i] == 0);
        }
        else
        {    
            sdefaults[i-1] = (dev->caps.port_types_default[i] == 0) ?
                             MLX4_PORT_TYPE_IB : MLX4_PORT_TYPE_ETH;
        }
    }

    mlx4_do_sense_ports(dev, stype, sdefaults);

    if (!mlx4_check_port_params(dev, stype))
    {
        for (i = 1; i <= dev->caps.num_ports; i++)
        {
            dev->caps.port_type_final[i] = stype[i-1];
        } 
        return mlx4_adjust_mfunc_ports(dev);
    }

    return -EINVAL;
}

#if 0
void mlx4_start_sense(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP))
		return;

	sense->resched = 1;
	KeSetTimerEx(&sense->timer, sense->interval, 0, &sense->timer_dpc);
}

void mlx4_stop_sense(struct mlx4_dev *dev)
{	 
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;

	sense->resched = 0;
}

static void  timer_dpc(
	IN struct _KDPC  *Dpc,
	IN PVOID  DeferredContext,
	IN PVOID  SystemArgument1,
	IN PVOID  SystemArgument2
	)
{
	struct mlx4_dev *dev = (struct mlx4_dev *)DeferredContext;
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	mlx4_sense_port(dev);
}

int mlx4_sense_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;
	int port;
	int err = 0;
	
	sense->dev = dev;
	
	sense->sense_work = IoAllocateWorkItem(dev->pdev->p_self_do);
	if(sense->sense_work == NULL)
	{
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to allocate work item from sensing thread\n", dev->pdev->name));
		err = -EFAULT;
		return err;
	}

	KeInitializeDpc(&sense->timer_dpc,timer_dpc,dev);
	KeInitializeTimer(&sense->timer_dpc);	 
	sense->interval.QuadPart = (-10)* (__int64)MLX4_SENSE_RANGE;
	
	for (port = 1; port <= dev->caps.num_ports; port++)
		sense->do_sense_port[port] = 1;
}

void mlx4_sense_cleanup(struct mlx4_dev *dev)]
{		
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;
	mlx4_stop_sense(dev);

	KeCancelTimer(&sense->timer);
	KeFlushQueuedDpcs();
	IoFreeWorkItem(sense->sense_work);
}

#endif
