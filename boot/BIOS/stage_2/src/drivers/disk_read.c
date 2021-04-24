#include "disk_read.h"

#include "ata/ata.h"
#include "ahci/ahci.h"

void ReadDisk(uint64_t LBA, void* buffer, uint32_t sectorCount)
{
    if(IsAHCI())
    {
        ReadSectorsAHCI(0, LBA, buffer, sectorCount);
    }
    else
    {
        ReadSectorsATA((uint64_t)buffer, LBA, (uint8_t)sectorCount);
    }
}