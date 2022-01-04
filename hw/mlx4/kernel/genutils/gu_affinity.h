#pragma once

#if WINVER >= 0x0601	
NTSTATUS AffinityToProcNumber(USHORT group, KAFFINITY affinity, PROCESSOR_NUMBER *proc_number);
void PrintNumaCpuConfiguration(USHORT verbose, PDEVICE_OBJECT Pdo);
#endif

