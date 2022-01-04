#include "gu_precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "gu_affinity.tmh"
#endif

#if WINVER >= 0x0601	
NTSTATUS AffinityToProcNumber(USHORT group, KAFFINITY affinity, PROCESSOR_NUMBER *proc_number)
{
    unsigned long cpu_number_in_group;
	
    if(proc_number == NULL || affinity == 0)
    {
        return STATUS_UNSUCCESSFUL;
    }
    
#if _M_IX86
    _BitScanForward(&cpu_number_in_group, affinity);
#else
    _BitScanForward64(&cpu_number_in_group, affinity);
#endif

    proc_number->Group = group;
    proc_number->Number = (UCHAR) cpu_number_in_group;

    return STATUS_SUCCESS;     
}

void PrintNumaCpuConfiguration(USHORT verbose, PDEVICE_OBJECT Pdo)
{
    USHORT NodeNumber;
    NTSTATUS Status;
    GROUP_AFFINITY GroupAffinity;
    USHORT NodeCount, HighestNodeNumber;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info = NULL;
    ULONG BufferSize = 0;
    
    Status = IoGetDeviceNumaNode(Pdo, &NodeNumber);
    
    if(Status != STATUS_SUCCESS)
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "Failed to get NUMA node for PDO %p Status 0x%x\n", Pdo, Status);
    }
    else
    {
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "PDO %p NUMA node %d\n", Pdo, NodeNumber);
        
        KeQueryNodeActiveAffinity(
          NodeNumber,
          &GroupAffinity,
          &NodeCount
          );

        GU_PRINT(TRACE_LEVEL_ERROR, GU, "PDO NUMA node %d: #Processors=%d ProcessorGroup=%d ProcessorAffinity=0x%p\n", 
            NodeNumber, NodeCount, GroupAffinity.Group, (void *) GroupAffinity.Mask);
    }
    
    HighestNodeNumber = KeQueryHighestNodeNumber();

    for(USHORT i = 0; i <= HighestNodeNumber; i++)
    {
        KeQueryNodeActiveAffinity(
          i,
          &GroupAffinity,
          &NodeCount
          );
        
        GU_PRINT(TRACE_LEVEL_ERROR, GU, "NUMA node %d: #Processors=%d ProcessorGroup=%d ProcessorAffinity=0x%p\n", 
            i, NodeCount, GroupAffinity.Group, (void *) GroupAffinity.Mask);
    }
    
    if(! verbose)
    {
        return;
    }
    
    //
    // Get required buffer size.
    //
    Status = KeQueryLogicalProcessorRelationship(NULL, RelationAll, NULL, &BufferSize);

    ASSERT(Status == STATUS_INFO_LENGTH_MISMATCH && BufferSize > 0);

    //
    // Allocate buffer (assume IRQL <= APC_LEVEL).
    //
    Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) ExAllocatePoolWithTag(PagedPool, BufferSize, ' gaT');
    if (Info == NULL)
    {
        return;
    }

    //
    // Get processor relationship information.
    //
    Status = KeQueryLogicalProcessorRelationship(NULL, RelationAll, Info, &BufferSize);

    if(Status != STATUS_SUCCESS)
    {
        return;
    }

    GU_PRINT(TRACE_LEVEL_ERROR, GU, "\n\nSystem processor information:\n");

    for(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pCurrInfo = Info;
        (char *) pCurrInfo < (char *) Info + BufferSize;
        pCurrInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) ((char *) pCurrInfo + pCurrInfo->Size))
    {
        switch(pCurrInfo->Relationship)
        {
        case RelationProcessorCore:
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "--Processor core-- Group=%d Mask=0x%p\n", 
                pCurrInfo->Processor.GroupMask[0].Group, 
                (void *) pCurrInfo->Processor.GroupMask[0].Mask );
            break;

        case RelationProcessorPackage:
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "--Processor package-- Group count %d\n", pCurrInfo->Processor.GroupCount);
            for(USHORT i = 0; i < pCurrInfo->Processor.GroupCount; i++)
            {
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "Group #%d: Group=%d Mask=0x%p\n", i, 
                    pCurrInfo->Processor.GroupMask[i].Group,
                    (void *) pCurrInfo->Processor.GroupMask[i].Mask);
            }
            break;

        case RelationNumaNode:
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "--Numa node-- Node #%d ProcessorGroup=%d ProcessorMask=0x%p\n", 
                pCurrInfo->NumaNode.NodeNumber, pCurrInfo->NumaNode.GroupMask.Group, (void *) pCurrInfo->NumaNode.GroupMask.Mask);
            break;

        case RelationGroup:
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "--Groups info-- MaxGroupCount=%d ActiveGroupCount=%d\n", 
                pCurrInfo->Group.MaximumGroupCount, pCurrInfo->Group.ActiveGroupCount);
            for(USHORT i = 0; i < pCurrInfo->Group.ActiveGroupCount; i++)
            {
                GU_PRINT(TRACE_LEVEL_ERROR, GU, "Group #%d: MaxProcCount=%d ActiveProcCount=%d ActiveProcMask=0x%p\n", i, 
                    pCurrInfo->Group.GroupInfo[i].MaximumProcessorCount,
                    pCurrInfo->Group.GroupInfo[i].ActiveProcessorCount,
                    (void *) pCurrInfo->Group.GroupInfo[i].ActiveProcessorMask);
            }
            break;

        case RelationCache:
            GU_PRINT(TRACE_LEVEL_ERROR, GU, "--Cache info-- Level=L%d, Associativity=%d, LineSize=%d bytes, \
                CacheSize=%d bytes, Type=%d, ProcGroup=%d, ProcMask=0x%p\n", 
                pCurrInfo->Cache.Level, 
                pCurrInfo->Cache.Associativity,
                pCurrInfo->Cache.LineSize,
                pCurrInfo->Cache.CacheSize,
                pCurrInfo->Cache.Type,
                pCurrInfo->Cache.GroupMask.Group,
                (void *) pCurrInfo->Cache.GroupMask.Mask);
            break;

        default:
            ASSERT(FALSE);
            break;
        }
    }

}

#endif

