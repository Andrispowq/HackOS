#include "ahci.h"

extern AHCIDriver* drivers[5];

int IsAHCI()
{
    if(drivers[0] == 0)
    {
        return 0;
    }

    return 1;
}

void ReadSectorsAHCI(uint8_t PortNumber, uint64_t LBA, void* buffer, uint32_t sectorCount)
{
    Port* port = drivers[0]->ports[PortNumber];
    Read(port, LBA, sectorCount, buffer);
}