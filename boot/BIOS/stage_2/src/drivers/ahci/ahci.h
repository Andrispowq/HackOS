#ifndef AHCI__H
#define AHCI__H

#include "libc/stdint.h"
#include "cpu/ahci/ahci.h"

void ReadSectorsAHCI(uint8_t PortNumber, uint64_t LBA, void* buffer, uint32_t sectorCount);
int IsAHCI();

#endif