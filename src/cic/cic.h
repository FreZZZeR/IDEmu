#ifndef IDEMU_CIC_H
#define IDEMU_CIC_H
#include "hw/core/boards.h"
#include "target/sh4/cpu.h"
/* contêiner de periféricos on-chip: visível em P4 (0xE0000000+) e no físico 0x1C000000+ (área 7) */
extern MemoryRegion *cic_oc;
void cic_map_dual(MemoryRegion *mr, hwaddr p4);
void cic_soc_init(MemoryRegion *sysmem, SuperHCPU *cpu, MachineState *machine);
/* tensão de alimentação publicada pela IOC no canal 2 (byte 5 do status), em décimos de volt; 0 = padrão (CIC_VOLT ou 11,6 V) */
extern volatile int cic_ioc_volt;
void cic_pci_init(MemoryRegion *sysmem, qemu_irq irq_fpga, qemu_irq irq_carmine);
#endif
