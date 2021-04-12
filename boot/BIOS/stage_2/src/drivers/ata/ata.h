#ifndef ATA_H
#define ATA_H

#include "libc/stdint.h"

void ReadSectorsATA(uint64_t target_address, uint32_t LBA, uint8_t sector_count);
void WriteSectorsATA(uint32_t LBA, uint8_t sector_count, uint32_t* bytes);

#endif