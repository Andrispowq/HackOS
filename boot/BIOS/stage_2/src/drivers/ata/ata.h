#ifndef ATA_H
#define ATA_H

#include "libc/stdint.h"

#define ATA_SR_BSY     0x80    // Busy
#define ATA_SR_DRDY    0x40    // Drive ready
#define ATA_SR_DF      0x20    // Drive write fault
#define ATA_SR_DSC     0x10    // Drive seek complete
#define ATA_SR_DRQ     0x08    // Data request ready
#define ATA_SR_CORR    0x04    // Corrected data
#define ATA_SR_IDX     0x02    // Index
#define ATA_SR_ERR     0x01    // Error

void ReadSectorsATA(uint64_t target_address, uint32_t LBA, uint8_t sector_count);
void WriteSectorsATA(uint32_t LBA, uint8_t sector_count, uint32_t* bytes);

#endif