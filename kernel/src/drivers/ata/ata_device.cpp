#include "ata_device.h"
#include "ata.h"

bool ATADevice::Read(uint64_t LBA, void* buffer, uint64_t size)
{
    return LBA < (1ULL << 28) && size && size <= 255 && size <= (1ULL << 28) - LBA &&
           ReadSectorsATA((uint64_t)buffer, (uint32_t)LBA, (uint8_t)size);
}

bool ATADevice::Write(uint64_t LBA, void* buffer, uint64_t size)
{
    return LBA < (1ULL << 28) && size && size <= 255 && size <= (1ULL << 28) - LBA &&
           WriteSectorsATA((uint32_t)LBA, (uint8_t)size, (uint16_t*)buffer);
}

bool ATADevice::Flush()
{
    return FlushATA();
}
