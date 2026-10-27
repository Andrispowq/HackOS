#include "ata_device.h"
#include "ata.h"

ATADevice::ATADevice() : sectors(IdentifyATA())
{
}

uint64_t ATADevice::SectorCount() const
{
    return sectors;
}

static bool TransferATA(uint64_t lba, void* buffer, uint64_t count, uint64_t capacity, bool write)
{
    if (!buffer || !count || lba >= capacity || count > capacity - lba)
    {
        return false;
    }

    while (count)
    {
        uint8_t chunk = count < 255 ? count : 255;
        bool ok = write ? WriteSectorsATA(lba, chunk, (uint16_t*)buffer) : ReadSectorsATA((uint64_t)buffer, lba, chunk);
        if (!ok)
        {
            return false;
        }

        lba += chunk;
        count -= chunk;
        buffer = (uint8_t*)buffer + chunk * 512;
    }

    return true;
}

bool ATADevice::Read(uint64_t lba, void* buffer, uint64_t count)
{
    return TransferATA(lba, buffer, count, sectors, false);
}

bool ATADevice::Write(uint64_t lba, void* buffer, uint64_t count)
{
    return TransferATA(lba, buffer, count, sectors, true);
}

bool ATADevice::Flush()
{
    return FlushATA();
}
