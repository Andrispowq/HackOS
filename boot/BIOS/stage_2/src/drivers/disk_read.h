#ifndef DISK_READ_H
#define DISK_READ_H

#include "libc/stdint.h"

void ReadDisk(uint64_t LBA, void* buffer, uint32_t sectorCount);

#endif