#ifndef ATA_H
#define ATA_H

#include "lib/stdint.h"

bool ReadSectorsATA(uint64_t target_address, uint32_t LBA, uint8_t sector_count);
bool WriteSectorsATA(uint32_t LBA, uint8_t sector_count, uint16_t* bytes);

bool FlushATA();

#endif

uint64_t IdentifyATA();
