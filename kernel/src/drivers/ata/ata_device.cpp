#include "ata_device.h"

#include "ata.h"

void Device::Read(uint64_t LBA, void* buffer, uint64_t size)
{
    ReadSectorsATA((uint64_t)buffer, (uint32_t)LBA, (uint8_t)size);
}

void Device::Write(uint64_t LBA, void* buffer, uint64_t size)
{
    WriteSectorsATA((uint32_t)LBA, (uint8_t)size, (uint32_t*)buffer);
}